package main

import (
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"net/url"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"testing"
)

// list calls handleList and follows next_cursor until the listing is complete.
func list(t *testing.T, path string, recursive bool) (paths []string, sawRecursive bool) {
	t.Helper()
	cursor := ""
	for {
		q := url.Values{"path": {path}}
		if recursive {
			q.Set("recursive", "true")
		}
		if cursor != "" {
			q.Set("cursor", cursor)
		}
		rec := httptest.NewRecorder()
		handleList(rec, httptest.NewRequest(http.MethodGet, "/v1/list?"+q.Encode(), nil))
		if rec.Code != http.StatusOK {
			t.Fatalf("list %q: status %d: %s", path, rec.Code, rec.Body.String())
		}
		var resp ListResponse
		if err := json.Unmarshal(rec.Body.Bytes(), &resp); err != nil {
			t.Fatalf("list %q: %v", path, err)
		}
		sawRecursive = resp.Recursive
		for _, it := range resp.Items {
			paths = append(paths, it.Path)
		}
		if resp.NextCursor == "" {
			return paths, sawRecursive
		}
		cursor = resp.NextCursor
	}
}

func writeFile(t *testing.T, root, rel string) {
	t.Helper()
	abs := filepath.Join(root, rel)
	if err := os.MkdirAll(filepath.Dir(abs), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(abs, []byte("x"), 0o644); err != nil {
		t.Fatal(err)
	}
}

func TestListRecursive(t *testing.T) {
	root := t.TempDir()
	*flagRoot = root
	writeFile(t, root, "data/x.csv")
	writeFile(t, root, "data/sub/y.csv")
	writeFile(t, root, "data/sub/deep/z.csv")
	if err := os.MkdirAll(filepath.Join(root, "data/empty"), 0o755); err != nil {
		t.Fatal(err)
	}

	flat, flatFlag := list(t, "/data", false)
	sort.Strings(flat)
	wantFlat := []string{"/data/empty", "/data/sub", "/data/x.csv"}
	if fmt.Sprint(flat) != fmt.Sprint(wantFlat) {
		t.Errorf("flat listing = %v, want %v", flat, wantFlat)
	}
	if flatFlag {
		t.Errorf("flat listing reported recursive=true")
	}

	all, recFlag := list(t, "/data", true)
	sort.Strings(all)
	wantAll := []string{
		"/data/empty", "/data/sub", "/data/sub/deep", "/data/sub/deep/z.csv",
		"/data/sub/y.csv", "/data/x.csv",
	}
	if fmt.Sprint(all) != fmt.Sprint(wantAll) {
		t.Errorf("recursive listing = %v, want %v", all, wantAll)
	}
	if !recFlag {
		t.Errorf("recursive listing did not report recursive=true")
	}
}

// A recursive listing larger than one page must come back complete and
// without duplicates when the client follows next_cursor.
func TestListRecursivePagination(t *testing.T) {
	root := t.TempDir()
	*flagRoot = root
	const n = 2300
	for i := 0; i < n; i++ {
		writeFile(t, root, fmt.Sprintf("d%02d/f%04d.csv", i%7, i))
	}

	all, _ := list(t, "/", true)
	seen := map[string]bool{}
	files := 0
	for _, p := range all {
		if seen[p] {
			t.Fatalf("duplicate entry %q", p)
		}
		seen[p] = true
		if filepath.Ext(p) == ".csv" {
			files++
		}
	}
	if files != n {
		t.Errorf("got %d files, want %d", files, n)
	}
}

func TestListMissingFolder(t *testing.T) {
	*flagRoot = t.TempDir()
	for _, recursive := range []string{"", "&recursive=true"} {
		rec := httptest.NewRecorder()
		handleList(rec, httptest.NewRequest(http.MethodGet, "/v1/list?path=/nope"+recursive, nil))
		if rec.Code != http.StatusNotFound {
			t.Errorf("missing folder (%q): status %d, want 404", recursive, rec.Code)
		}
	}
}

// An aborted upload removes the partial file and forgets the session.
func TestUploadAbort(t *testing.T) {
	root := t.TempDir()
	*flagRoot = root

	rec := httptest.NewRecorder()
	handleUploadStart(rec, httptest.NewRequest(http.MethodPost, "/v1/upload/start",
		strings.NewReader(`{"path":"/out/partial.bin","size":-1}`)))
	if rec.Code != http.StatusOK {
		t.Fatalf("start: status %d: %s", rec.Code, rec.Body.String())
	}
	var started struct {
		SessionID string `json:"session_id"`
	}
	if err := json.Unmarshal(rec.Body.Bytes(), &started); err != nil || started.SessionID == "" {
		t.Fatalf("start: bad response %q", rec.Body.String())
	}
	q := "?session=" + started.SessionID

	rec = httptest.NewRecorder()
	handleUploadChunk(rec, httptest.NewRequest(http.MethodPut, "/v1/upload/chunk"+q+"&offset=0",
		strings.NewReader("hello")))
	if rec.Code != http.StatusAccepted {
		t.Fatalf("chunk: status %d", rec.Code)
	}
	partial := filepath.Join(root, "out/partial.bin")
	if _, err := os.Stat(partial); err != nil {
		t.Fatalf("partial file missing before abort: %v", err)
	}

	rec = httptest.NewRecorder()
	handleUploadAbort(rec, httptest.NewRequest(http.MethodDelete, "/v1/upload/abort"+q, nil))
	if rec.Code != http.StatusNoContent {
		t.Fatalf("abort: status %d", rec.Code)
	}
	if _, err := os.Stat(partial); !os.IsNotExist(err) {
		t.Errorf("partial file still present after abort (err=%v)", err)
	}

	rec = httptest.NewRecorder()
	handleUploadAbort(rec, httptest.NewRequest(http.MethodDelete, "/v1/upload/abort"+q, nil))
	if rec.Code != http.StatusNotFound {
		t.Errorf("second abort: status %d, want 404", rec.Code)
	}
}
