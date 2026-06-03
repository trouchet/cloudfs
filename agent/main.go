// cloudfs-agent — lightweight filesystem HTTP bridge for DuckDB cloudfs.
//
// Deploy on any Linux server:
//   ./cloudfs-agent --token my-secret-token --port 8765 --root /data
//
// With TLS (recommended):
//   ./cloudfs-agent --token my-secret-token --port 8766 --tls \
//     --cert /etc/cloudfs/cert.pem --key /etc/cloudfs/key.pem --root /data
//
// Systemd unit (runs as non-root, reads /data):
//   [Service]
//   ExecStart=/usr/local/bin/cloudfs-agent --token ${CLOUDFS_TOKEN} --root /data
//   Restart=always

package main

import (
    "crypto/subtle"
    "encoding/json"
    "flag"
    "fmt"
    "io"
    "log"
    "mime"
    "net/http"
    "os"
    "path/filepath"
    "strconv"
    "strings"
    "time"
)

// ─── Config ───────────────────────────────────────────────────────────────────
var (
    flagToken    = flag.String("token", "", "Bearer token for authentication (required)")
    flagPort     = flag.Int("port", 8765, "Port to listen on")
    flagRoot     = flag.String("root", "/", "Root directory to expose")
    flagTLS      = flag.Bool("tls", false, "Enable TLS")
    flagCert     = flag.String("cert", "", "TLS certificate file")
    flagKey      = flag.String("key", "", "TLS private key file")
    flagReadOnly = flag.Bool("read-only", false, "Disable write operations")
    flagMaxSize  = flag.Int64("max-upload-mb", 10240, "Max upload size in MB")
    flagVersion  = "0.1.0"
)

// ─── Item represents a file or directory ─────────────────────────────────────
type Item struct {
    Path      string `json:"path"`
    Name      string `json:"name"`
    Size      int64  `json:"size"`
    IsDir     bool   `json:"is_dir"`
    MtimeMs   int64  `json:"mtime_ms"`
    Etag      string `json:"etag"`
}

type ListResponse struct {
    Items      []Item `json:"items"`
    NextCursor string `json:"next_cursor"`
}

// ─── Upload sessions ──────────────────────────────────────────────────────────
type UploadSession struct {
    ID        string
    Path      string
    CreatedAt time.Time
    File      *os.File
}

var uploadSessions = map[string]*UploadSession{}

// ─── Auth middleware ──────────────────────────────────────────────────────────
func authMiddleware(next http.HandlerFunc) http.HandlerFunc {
    return func(w http.ResponseWriter, r *http.Request) {
        auth := r.Header.Get("Authorization")
        if !strings.HasPrefix(auth, "Bearer ") {
            http.Error(w, "Unauthorized", http.StatusUnauthorized)
            return
        }
        provided := strings.TrimPrefix(auth, "Bearer ")
        if subtle.ConstantTimeCompare([]byte(provided), []byte(*flagToken)) != 1 {
            http.Error(w, "Forbidden", http.StatusForbidden)
            return
        }
        next(w, r)
    }
}

// ─── Path validation ──────────────────────────────────────────────────────────
func safePath(p string) (string, error) {
    // Resolve to absolute path within root
    abs := filepath.Join(*flagRoot, filepath.Clean("/"+p))
    if !strings.HasPrefix(abs, filepath.Clean(*flagRoot)) {
        return "", fmt.Errorf("path escapes root")
    }
    return abs, nil
}

// ─── Handlers ─────────────────────────────────────────────────────────────────

// GET /v1/ping
func handlePing(w http.ResponseWriter, r *http.Request) {
    w.Header().Set("Content-Type", "application/json")
    fmt.Fprintf(w, `{"status":"ok","version":"%s","root":"%s"}`, flagVersion, *flagRoot)
}

// GET /v1/stat?path=...
func handleStat(w http.ResponseWriter, r *http.Request) {
    p, err := safePath(r.URL.Query().Get("path"))
    if err != nil { http.Error(w, err.Error(), 400); return }

    info, err := os.Lstat(p)
    if os.IsNotExist(err) { http.Error(w, "not found", 404); return }
    if err != nil { http.Error(w, err.Error(), 500); return }

    relPath := strings.TrimPrefix(p, filepath.Clean(*flagRoot))
    if !strings.HasPrefix(relPath, "/") { relPath = "/" + relPath }
    item := Item{
        Path:    relPath,
        Name:    info.Name(),
        Size:    info.Size(),
        IsDir:   info.IsDir(),
        MtimeMs: info.ModTime().UnixMilli(),
        Etag:    fmt.Sprintf("%d-%d", info.Size(), info.ModTime().UnixNano()),
    }
    w.Header().Set("Content-Type", "application/json")
    json.NewEncoder(w).Encode(item)
}

// GET /v1/read?path=...  (Range header supported)
func handleRead(w http.ResponseWriter, r *http.Request) {
    p, err := safePath(r.URL.Query().Get("path"))
    if err != nil { http.Error(w, err.Error(), 400); return }

    f, err := os.Open(p)
    if os.IsNotExist(err) { http.Error(w, "not found", 404); return }
    if err != nil { http.Error(w, err.Error(), 500); return }
    defer f.Close()

    info, _ := f.Stat()
    // Detect content type
    ext := filepath.Ext(p)
    ct := mime.TypeByExtension(ext)
    if ct == "" { ct = "application/octet-stream" }
    w.Header().Set("Content-Type", ct)
    w.Header().Set("ETag", fmt.Sprintf(`"%d-%d"`, info.Size(), info.ModTime().UnixNano()))
    w.Header().Set("Accept-Ranges", "bytes")

    // Let the standard library handle Range, If-None-Match, etc.
    http.ServeContent(w, r, info.Name(), info.ModTime(), f)
}

// GET /v1/list?path=...&cursor=...
func handleList(w http.ResponseWriter, r *http.Request) {
    p, err := safePath(r.URL.Query().Get("path"))
    if err != nil { http.Error(w, err.Error(), 400); return }

    entries, err := os.ReadDir(p)
    if os.IsNotExist(err) { http.Error(w, "not found", 404); return }
    if err != nil { http.Error(w, err.Error(), 500); return }

    // Simple cursor: numeric offset into sorted entries
    cursor   := r.URL.Query().Get("cursor")
    offset   := 0
    pageSize := 1000
    if cursor != "" { offset, _ = strconv.Atoi(cursor) }

    items := []Item{}
    end := offset + pageSize
    if end > len(entries) { end = len(entries) }
    for _, e := range entries[offset:end] {
        info, err := e.Info()
        if err != nil { continue }
        rel := strings.TrimPrefix(filepath.Join(p, e.Name()), filepath.Clean(*flagRoot))
        if !strings.HasPrefix(rel, "/") { rel = "/" + rel }
        items = append(items, Item{
            Path:    rel,
            Name:    e.Name(),
            Size:    info.Size(),
            IsDir:   e.IsDir(),
            MtimeMs: info.ModTime().UnixMilli(),
            Etag:    fmt.Sprintf("%d-%d", info.Size(), info.ModTime().UnixNano()),
        })
    }

    next := ""
    if end < len(entries) { next = strconv.Itoa(end) }

    w.Header().Set("Content-Type", "application/json")
    json.NewEncoder(w).Encode(ListResponse{Items: items, NextCursor: next})
}

// POST /v1/mkdir  body: {"path":"..."}
func handleMkdir(w http.ResponseWriter, r *http.Request) {
    if *flagReadOnly { http.Error(w, "read-only mode", 403); return }
    var req struct{ Path string `json:"path"` }
    json.NewDecoder(r.Body).Decode(&req)
    p, err := safePath(req.Path)
    if err != nil { http.Error(w, err.Error(), 400); return }
    if err := os.MkdirAll(p, 0755); err != nil { http.Error(w, err.Error(), 500); return }
    w.WriteHeader(200)
}

// DELETE /v1/delete?path=...
func handleDelete(w http.ResponseWriter, r *http.Request) {
    if *flagReadOnly { http.Error(w, "read-only mode", 403); return }
    p, err := safePath(r.URL.Query().Get("path"))
    if err != nil { http.Error(w, err.Error(), 400); return }
    if err := os.RemoveAll(p); err != nil { http.Error(w, err.Error(), 500); return }
    w.WriteHeader(204)
}

// POST /v1/upload/start  body: {"path":"...", "size": N}
func handleUploadStart(w http.ResponseWriter, r *http.Request) {
    if *flagReadOnly { http.Error(w, "read-only mode", 403); return }
    var req struct {
        Path string `json:"path"`
        Size int64  `json:"size"`
    }
    json.NewDecoder(r.Body).Decode(&req)
    p, err := safePath(req.Path)
    if err != nil { http.Error(w, err.Error(), 400); return }

    os.MkdirAll(filepath.Dir(p), 0755)
    f, err := os.Create(p)
    if err != nil { http.Error(w, err.Error(), 500); return }

    id := fmt.Sprintf("%d", time.Now().UnixNano())
    uploadSessions[id] = &UploadSession{ID: id, Path: p, CreatedAt: time.Now(), File: f}

    w.Header().Set("Content-Type", "application/json")
    fmt.Fprintf(w, `{"session_id":"%s"}`, id)
}

// PUT /v1/upload/chunk?session=...&offset=N
func handleUploadChunk(w http.ResponseWriter, r *http.Request) {
    if *flagReadOnly { http.Error(w, "read-only mode", 403); return }
    id  := r.URL.Query().Get("session")
    off, _ := strconv.ParseInt(r.URL.Query().Get("offset"), 10, 64)

    sess, ok := uploadSessions[id]
    if !ok { http.Error(w, "unknown session", 404); return }

    sess.File.Seek(off, io.SeekStart)
    if _, err := io.Copy(sess.File, r.Body); err != nil {
        http.Error(w, err.Error(), 500); return
    }
    w.WriteHeader(202)
}

// POST /v1/upload/finish?session=...
func handleUploadFinish(w http.ResponseWriter, r *http.Request) {
    if *flagReadOnly { http.Error(w, "read-only mode", 403); return }
    id   := r.URL.Query().Get("session")
    sess, ok := uploadSessions[id]
    if !ok { http.Error(w, "unknown session", 404); return }

    // Write any remaining bytes from the body
    io.Copy(sess.File, r.Body)
    sess.File.Close()
    delete(uploadSessions, id)

    info, _ := os.Stat(sess.Path)
    rel := strings.TrimPrefix(sess.Path, filepath.Clean(*flagRoot))
    if !strings.HasPrefix(rel, "/") { rel = "/" + rel }
    item := Item{Path: rel, Name: filepath.Base(sess.Path), Size: info.Size()}
    w.Header().Set("Content-Type", "application/json")
    w.WriteHeader(201)
    json.NewEncoder(w).Encode(item)
}

// ─── Main ─────────────────────────────────────────────────────────────────────
func main() {
    flag.Parse()

    if *flagToken == "" {
        log.Fatal("--token is required. Generate one with: openssl rand -hex 32")
    }
    if *flagTLS && (*flagCert == "" || *flagKey == "") {
        log.Fatal("--tls requires --cert and --key")
    }

    // Clean and validate root
    root, err := filepath.Abs(*flagRoot)
    if err != nil { log.Fatalf("invalid root: %v", err) }
    *flagRoot = root
    if _, err := os.Stat(*flagRoot); os.IsNotExist(err) {
        log.Fatalf("root directory does not exist: %s", *flagRoot)
    }

    mux := http.NewServeMux()
    mux.HandleFunc("/v1/ping",          authMiddleware(handlePing))
    mux.HandleFunc("/v1/stat",          authMiddleware(handleStat))
    mux.HandleFunc("/v1/read",          authMiddleware(handleRead))
    mux.HandleFunc("/v1/list",          authMiddleware(handleList))
    mux.HandleFunc("/v1/mkdir",         authMiddleware(handleMkdir))
    mux.HandleFunc("/v1/delete",        authMiddleware(handleDelete))
    mux.HandleFunc("/v1/upload/start",  authMiddleware(handleUploadStart))
    mux.HandleFunc("/v1/upload/chunk",  authMiddleware(handleUploadChunk))
    mux.HandleFunc("/v1/upload/finish", authMiddleware(handleUploadFinish))

    addr := fmt.Sprintf(":%d", *flagPort)
    mode := "HTTP"
    if *flagTLS { mode = "HTTPS" }
    log.Printf("cloudfs-agent v%s | %s | root=%s | port=%d | read-only=%v",
                flagVersion, mode, *flagRoot, *flagPort, *flagReadOnly)

    if *flagTLS {
        log.Fatal(http.ListenAndServeTLS(addr, *flagCert, *flagKey, mux))
    } else {
        log.Fatal(http.ListenAndServe(addr, mux))
    }
}
