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
	"sync"
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
	Path    string `json:"path"`
	Name    string `json:"name"`
	Size    int64  `json:"size"`
	IsDir   bool   `json:"is_dir"`
	MtimeMs int64  `json:"mtime_ms"`
	Etag    string `json:"etag"`
}

type ListResponse struct {
	// Recursive is true when the listing covers all descendants. Clients use it
	// to detect agents that predate ?recursive=true and ignore the parameter.
	// It is the first field so it is also the first key in the JSON output.
	Recursive  bool   `json:"recursive"`
	Items      []Item `json:"items"`
	NextCursor string `json:"next_cursor"`
}

// ─── Upload sessions ──────────────────────────────────────────────────────────
type UploadSession struct {
	ID           string
	Path         string
	CreatedAt    time.Time
	File         *os.File
	BytesWritten int64
}

var uploadSessions = map[string]*UploadSession{}
var uploadMu sync.RWMutex // Protects uploadSessions map

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
	if err != nil {
		http.Error(w, err.Error(), 400)
		return
	}

	info, err := os.Lstat(p)
	if os.IsNotExist(err) {
		http.Error(w, "not found", 404)
		return
	}
	if err != nil {
		http.Error(w, err.Error(), 500)
		return
	}

	relPath := strings.TrimPrefix(p, filepath.Clean(*flagRoot))
	if !strings.HasPrefix(relPath, "/") {
		relPath = "/" + relPath
	}
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
	if err != nil {
		http.Error(w, err.Error(), 400)
		return
	}

	f, err := os.Open(p)
	if os.IsNotExist(err) {
		http.Error(w, "not found", 404)
		return
	}
	if err != nil {
		http.Error(w, err.Error(), 500)
		return
	}
	defer f.Close()

	info, _ := f.Stat()
	// Detect content type
	ext := filepath.Ext(p)
	ct := mime.TypeByExtension(ext)
	if ct == "" {
		ct = "application/octet-stream"
	}
	w.Header().Set("Content-Type", ct)
	w.Header().Set("ETag", fmt.Sprintf(`"%d-%d"`, info.Size(), info.ModTime().UnixNano()))
	w.Header().Set("Accept-Ranges", "bytes")

	// Let the standard library handle Range, If-None-Match, etc.
	http.ServeContent(w, r, info.Name(), info.ModTime(), f)
}

// listEntry is one directory entry found by handleList.
type listEntry struct {
	abs  string
	name string
	info os.FileInfo
}

// GET /v1/list?path=...&cursor=...&recursive=true
//
// Lists the immediate children of path, or every descendant when
// recursive=true. Pagination uses a numeric offset into the (deterministically
// ordered) listing; follow-up requests must repeat path and recursive.
func handleList(w http.ResponseWriter, r *http.Request) {
	p, err := safePath(r.URL.Query().Get("path"))
	if err != nil {
		http.Error(w, err.Error(), 400)
		return
	}
	recursive := r.URL.Query().Get("recursive") == "true"

	var entries []listEntry
	if recursive {
		// WalkDir visits in lexical order and does not follow symlinks, so
		// the order is stable across pages and link cycles cannot loop.
		err = filepath.WalkDir(p, func(abs string, d os.DirEntry, walkErr error) error {
			if walkErr != nil {
				if abs == p {
					return walkErr
				}
				return nil // unreadable subtree: skip it, keep the rest
			}
			if abs == p {
				return nil
			}
			info, infoErr := d.Info()
			if infoErr != nil {
				return nil
			}
			entries = append(entries, listEntry{abs: abs, name: d.Name(), info: info})
			return nil
		})
	} else {
		var dirEntries []os.DirEntry
		dirEntries, err = os.ReadDir(p)
		for _, e := range dirEntries {
			info, infoErr := e.Info()
			if infoErr != nil {
				continue
			}
			entries = append(entries, listEntry{abs: filepath.Join(p, e.Name()), name: e.Name(), info: info})
		}
	}
	if os.IsNotExist(err) {
		http.Error(w, "not found", 404)
		return
	}
	if err != nil {
		http.Error(w, err.Error(), 500)
		return
	}

	// Simple cursor: numeric offset into the ordered entries
	offset := 0
	pageSize := 1000
	if cursor := r.URL.Query().Get("cursor"); cursor != "" {
		offset, err = strconv.Atoi(cursor)
		if err != nil || offset < 0 {
			http.Error(w, "invalid cursor", 400)
			return
		}
	}
	if offset > len(entries) {
		offset = len(entries)
	}
	end := offset + pageSize
	if end > len(entries) {
		end = len(entries)
	}

	root := filepath.Clean(*flagRoot)
	items := []Item{}
	for _, e := range entries[offset:end] {
		rel := filepath.ToSlash(strings.TrimPrefix(e.abs, root))
		if !strings.HasPrefix(rel, "/") {
			rel = "/" + rel
		}
		items = append(items, Item{
			Path:    rel,
			Name:    e.name,
			Size:    e.info.Size(),
			IsDir:   e.info.IsDir(),
			MtimeMs: e.info.ModTime().UnixMilli(),
			Etag:    fmt.Sprintf("%d-%d", e.info.Size(), e.info.ModTime().UnixNano()),
		})
	}

	next := ""
	if end < len(entries) {
		next = strconv.Itoa(end)
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(ListResponse{Recursive: recursive, Items: items, NextCursor: next})
}

// POST /v1/mkdir  body: {"path":"..."}
func handleMkdir(w http.ResponseWriter, r *http.Request) {
	if *flagReadOnly {
		http.Error(w, "read-only mode", 403)
		return
	}
	var req struct {
		Path string `json:"path"`
	}
	json.NewDecoder(r.Body).Decode(&req)
	p, err := safePath(req.Path)
	if err != nil {
		http.Error(w, err.Error(), 400)
		return
	}
	if err := os.MkdirAll(p, 0755); err != nil {
		http.Error(w, err.Error(), 500)
		return
	}
	w.WriteHeader(200)
}

// DELETE /v1/delete?path=...
func handleDelete(w http.ResponseWriter, r *http.Request) {
	if *flagReadOnly {
		http.Error(w, "read-only mode", 403)
		return
	}
	p, err := safePath(r.URL.Query().Get("path"))
	if err != nil {
		http.Error(w, err.Error(), 400)
		return
	}
	if err := os.RemoveAll(p); err != nil {
		http.Error(w, err.Error(), 500)
		return
	}
	w.WriteHeader(204)
}

// POST /v1/move  body: {"from":"...","to":"..."}
//
// Renames a file or directory within the root. An existing destination file is
// replaced atomically, which is what DuckDB relies on when COPY overwrites a
// file (it writes a temporary file and moves it over the target).
func handleMove(w http.ResponseWriter, r *http.Request) {
	if *flagReadOnly {
		http.Error(w, "read-only mode", 403)
		return
	}
	var req struct {
		From string `json:"from"`
		To   string `json:"to"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil || req.From == "" || req.To == "" {
		http.Error(w, "from and to are required", 400)
		return
	}
	from, err := safePath(req.From)
	if err != nil {
		http.Error(w, err.Error(), 400)
		return
	}
	to, err := safePath(req.To)
	if err != nil {
		http.Error(w, err.Error(), 400)
		return
	}
	root := filepath.Clean(*flagRoot)
	if from == root || to == root {
		http.Error(w, "cannot move the root", 400)
		return
	}
	if _, err := os.Lstat(from); os.IsNotExist(err) {
		http.Error(w, "not found", 404)
		return
	}
	if err := os.Rename(from, to); err != nil {
		if os.IsNotExist(err) {
			http.Error(w, "destination folder not found", 404)
			return
		}
		http.Error(w, err.Error(), 409)
		return
	}
	w.WriteHeader(200)
}

// POST /v1/upload/start  body: {"path":"...", "size": N}
func handleUploadStart(w http.ResponseWriter, r *http.Request) {
	if *flagReadOnly {
		http.Error(w, "read-only mode", 403)
		return
	}
	var req struct {
		Path string `json:"path"`
		Size int64  `json:"size"`
	}
	json.NewDecoder(r.Body).Decode(&req)

	// Enforce upload size limit
	maxBytes := *flagMaxSize * 1024 * 1024
	if req.Size > maxBytes {
		http.Error(w, fmt.Sprintf("upload too large: %d MB (max %d MB)", req.Size/(1024*1024), *flagMaxSize), 413)
		return
	}

	p, err := safePath(req.Path)
	if err != nil {
		http.Error(w, err.Error(), 400)
		return
	}

	os.MkdirAll(filepath.Dir(p), 0755)
	f, err := os.Create(p)
	if err != nil {
		http.Error(w, err.Error(), 500)
		return
	}

	id := fmt.Sprintf("%d", time.Now().UnixNano())

	uploadMu.Lock()
	uploadSessions[id] = &UploadSession{ID: id, Path: p, CreatedAt: time.Now(), File: f, BytesWritten: 0}
	uploadMu.Unlock()

	w.Header().Set("Content-Type", "application/json")
	fmt.Fprintf(w, `{"session_id":"%s"}`, id)
}

// PUT /v1/upload/chunk?session=...&offset=N
func handleUploadChunk(w http.ResponseWriter, r *http.Request) {
	if *flagReadOnly {
		http.Error(w, "read-only mode", 403)
		return
	}
	id := r.URL.Query().Get("session")
	off, _ := strconv.ParseInt(r.URL.Query().Get("offset"), 10, 64)

	uploadMu.RLock()
	sess, ok := uploadSessions[id]
	uploadMu.RUnlock()

	if !ok {
		http.Error(w, "unknown session", 404)
		return
	}

	sess.File.Seek(off, io.SeekStart)
	n, err := io.Copy(sess.File, r.Body)
	if err != nil {
		http.Error(w, err.Error(), 500)
		return
	}

	uploadMu.Lock()
	sess.BytesWritten += n
	maxBytes := *flagMaxSize * 1024 * 1024
	if sess.BytesWritten > maxBytes {
		// Exceeded limit during upload - cleanup
		sess.File.Close()
		os.Remove(sess.Path)
		delete(uploadSessions, id)
		uploadMu.Unlock()
		http.Error(w, fmt.Sprintf("upload exceeded limit: %d MB", *flagMaxSize), 413)
		return
	}
	uploadMu.Unlock()

	w.WriteHeader(202)
}

// POST /v1/upload/finish?session=...
func handleUploadFinish(w http.ResponseWriter, r *http.Request) {
	if *flagReadOnly {
		http.Error(w, "read-only mode", 403)
		return
	}
	id := r.URL.Query().Get("session")

	uploadMu.Lock()
	sess, ok := uploadSessions[id]
	if !ok {
		uploadMu.Unlock()
		http.Error(w, "unknown session", 404)
		return
	}
	delete(uploadSessions, id)
	uploadMu.Unlock()

	// Write any remaining bytes from the body
	n, _ := io.Copy(sess.File, r.Body)
	sess.BytesWritten += n
	sess.File.Close()

	// Final size check
	maxBytes := *flagMaxSize * 1024 * 1024
	if sess.BytesWritten > maxBytes {
		os.Remove(sess.Path)
		http.Error(w, fmt.Sprintf("upload exceeded limit: %d MB", *flagMaxSize), 413)
		return
	}

	info, _ := os.Stat(sess.Path)
	rel := strings.TrimPrefix(sess.Path, filepath.Clean(*flagRoot))
	if !strings.HasPrefix(rel, "/") {
		rel = "/" + rel
	}
	item := Item{Path: rel, Name: filepath.Base(sess.Path), Size: info.Size()}
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(201)
	json.NewEncoder(w).Encode(item)
}

// DELETE /v1/upload/abort?session=...
//
// Discards an unfinished upload: closes the session and removes the partial
// file. Unknown sessions answer 404 (already finished, aborted or expired).
func handleUploadAbort(w http.ResponseWriter, r *http.Request) {
	if *flagReadOnly {
		http.Error(w, "read-only mode", 403)
		return
	}
	id := r.URL.Query().Get("session")
	uploadMu.Lock()
	sess, ok := uploadSessions[id]
	if ok {
		delete(uploadSessions, id)
	}
	uploadMu.Unlock()
	if !ok {
		http.Error(w, "unknown session", 404)
		return
	}
	sess.File.Close()
	os.Remove(sess.Path)
	w.WriteHeader(204)
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
	if err != nil {
		log.Fatalf("invalid root: %v", err)
	}
	*flagRoot = root
	if _, err := os.Stat(*flagRoot); os.IsNotExist(err) {
		log.Fatalf("root directory does not exist: %s", *flagRoot)
	}

	// Start background cleanup for stale upload sessions
	go cleanupStaleSessions()

	mux := http.NewServeMux()
	mux.HandleFunc("/v1/ping", authMiddleware(handlePing))
	mux.HandleFunc("/v1/stat", authMiddleware(handleStat))
	mux.HandleFunc("/v1/read", authMiddleware(handleRead))
	mux.HandleFunc("/v1/list", authMiddleware(handleList))
	mux.HandleFunc("/v1/mkdir", authMiddleware(handleMkdir))
	mux.HandleFunc("/v1/delete", authMiddleware(handleDelete))
	mux.HandleFunc("/v1/move", authMiddleware(handleMove))
	mux.HandleFunc("/v1/upload/start", authMiddleware(handleUploadStart))
	mux.HandleFunc("/v1/upload/chunk", authMiddleware(handleUploadChunk))
	mux.HandleFunc("/v1/upload/finish", authMiddleware(handleUploadFinish))
	mux.HandleFunc("/v1/upload/abort", authMiddleware(handleUploadAbort))

	addr := fmt.Sprintf(":%d", *flagPort)
	mode := "HTTP"
	if *flagTLS {
		mode = "HTTPS"
	}
	log.Printf("cloudfs-agent v%s | %s | root=%s | port=%d | read-only=%v",
		flagVersion, mode, *flagRoot, *flagPort, *flagReadOnly)

	if *flagTLS {
		log.Fatal(http.ListenAndServeTLS(addr, *flagCert, *flagKey, mux))
	} else {
		log.Fatal(http.ListenAndServe(addr, mux))
	}
}

// cleanupStaleSessions removes upload sessions older than 1 hour
func cleanupStaleSessions() {
	ticker := time.NewTicker(10 * time.Minute)
	defer ticker.Stop()
	for range ticker.C {
		uploadMu.Lock()
		now := time.Now()
		for id, sess := range uploadSessions {
			if now.Sub(sess.CreatedAt) > time.Hour {
				sess.File.Close()
				os.Remove(sess.Path)
				delete(uploadSessions, id)
				log.Printf("cleaned up stale upload session: %s", id)
			}
		}
		uploadMu.Unlock()
	}
}
