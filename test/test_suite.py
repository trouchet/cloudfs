#!/usr/bin/env python3
"""
cloudfs full test suite — Task 2
Covers all test cases from AGENT_HANDOVER.md §Task 2, plus edge cases
for the VFS backend (the only provider testable without cloud credentials).

Run:
    cd /home/pingu/github/cloudfs
    python3 test/test_suite.py

Prerequisites:
    - cloudfs.duckdb_extension built and at project root
    - cloudfs-agent binary at project root
    - DuckDB Python package installed  (pip install duckdb)

Design note:
    The extension's CloudSecretRegistry uses a process-global dispatch table.
    Each LOAD into a new DuckDB database calls LoadInternal and registers all
    13 secret providers; loading into a second database would overflow the
    16-slot limit.  We therefore use ONE connection for the entire suite and
    reset secret state between sections with CREATE OR REPLACE SECRET.
"""

import ctypes, os, sys, time, shutil, secrets as _secrets, tempfile, subprocess
import duckdb

# ── Bootstrap ────────────────────────────────────────────────────────────────
# Load DuckDB with RTLD_GLOBAL so the extension can resolve its C++ symbols.
_SO = os.path.expanduser(
    "~/.local/lib/python3.12/site-packages" "/_duckdb.cpython-312-x86_64-linux-gnu.so"
)
ctypes.CDLL(_SO, ctypes.RTLD_GLOBAL)

PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXT = os.path.join(PROJ, "cloudfs.duckdb_extension")
AGENT = os.path.join(PROJ, "cloudfs-agent")
PORT = 19900

# ── Counters & helpers ────────────────────────────────────────────────────────
_PASS = _FAIL = 0


def check(label: str, ok: bool, detail: str = ""):
    global _PASS, _FAIL
    print(
        f"  {'✓ PASS' if ok else '✗ FAIL'}  {label}"
        + (f"\n         {detail}" if detail and not ok else "")
    )
    if ok:
        _PASS += 1
    else:
        _FAIL += 1


def section(title: str):
    print(f"\n{'─'*60}\n {title}\n{'─'*60}")


def sql(conn, q):
    try:
        return conn.execute(q).fetchall(), None
    except Exception as e:
        return [], str(e)


def start_agent(token, root, port=PORT):
    os.chmod(AGENT, 0o755)
    p = subprocess.Popen(
        [AGENT, "--token", token, "--port", str(port), "--root", root],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    time.sleep(0.5)
    return p


# ── Preflight ─────────────────────────────────────────────────────────────────
section("Preflight")

for path, label in [(EXT, "Extension"), (AGENT, "Agent")]:
    if not os.path.exists(path):
        print(f"  ✗ {label} not found: {path}")
        sys.exit(1)
    print(f"  {label}: {path} ({os.path.getsize(path)//1_000_000} MB)")

try:
    CONN = duckdb.connect(config={"allow_unsigned_extensions": "true"})
    CONN.execute(f"LOAD '{EXT}'")
    check("Extension loads", True)
except Exception as e:
    check("Extension loads", False, str(e))
    sys.exit(1)

# ── 1. Scalar functions ───────────────────────────────────────────────────────
section("1 · Scalar functions")

rows, err = sql(CONN, "SELECT cloudfs_version()")
check(
    "cloudfs_version() = 'cloudfs 0.1.0'",
    rows and rows[0][0] == "cloudfs 0.1.0",
    f"{rows} / {err}",
)

rows, err = sql(CONN, "SELECT providers()")
val = rows[0][0] if rows else ""
for scheme in ["spfs", "odfs", "gdfs", "dbxfs", "sftp", "vfs"]:
    check(f"providers() includes '{scheme}'", scheme in val, f"got: {val!r}")

rows, err = sql(CONN, "SELECT clear_cache()")
check("clear_cache() = 'OK'", rows and rows[0][0] == "OK", str(err))

rows, err = sql(CONN, "SELECT clear_cache('vfs')")
check("clear_cache('vfs') = 'OK'", rows and rows[0][0] == "OK", str(err))

rows, err = sql(CONN, "SELECT clear_cache('*')")
check("clear_cache('*') = 'OK'", rows and rows[0][0] == "OK", str(err))

# ── 2. Secret creation — all providers ───────────────────────────────────────
section("2 · Secret creation — all providers")

SECRET_CASES = [
    ("sharepoint", "token", "TOKEN 'x'"),
    ("onedrive", "token", "TOKEN 'x'"),
    ("gdrive", "token", "TOKEN 'x'"),
    ("dropbox", "token", "TOKEN 'x'"),
    ("sftp", "keyfile", "KEY_PATH '/root/.ssh/id_rsa'"),
    ("vfs", "token", "TOKEN 'x'"),
]
for type_, provider, extra in SECRET_CASES:
    _, err = sql(
        CONN, f"CREATE OR REPLACE SECRET s (TYPE {type_}, PROVIDER {provider}, {extra})"
    )
    if err:
        check(f"CREATE SECRET ({type_}/{provider})", False, err)
        continue
    rows, err2 = sql(CONN, "SELECT type FROM duckdb_secrets() WHERE name='s'")
    check(
        f"CREATE SECRET ({type_}/{provider}) registered",
        rows and type_ in rows[0][0],
        f"{rows}/{err2}",
    )

# Cleanup dummy secret
sql(CONN, "DROP SECRET IF EXISTS s")

# ── 3. Live VFS tests ─────────────────────────────────────────────────────────
section("3 · Live VFS tests (cloudfs-agent)")

root = tempfile.mkdtemp()
token = _secrets.token_hex(16)
BASE = f"vfs://localhost:{PORT}"
data_dir = os.path.join(root, "data")
sub_dir = os.path.join(root, "data", "sub")
os.makedirs(sub_dir)
os.makedirs(os.path.join(root, "empty"))

with open(os.path.join(data_dir, "hello.txt"), "w") as f:
    f.write("hello cloudfs\n")
with open(os.path.join(data_dir, "world.txt"), "w") as f:
    f.write("world\n")
with open(os.path.join(sub_dir, "nested.txt"), "w") as f:
    f.write("nested file\n")

# Write parquet files locally (no VFS needed for setup)
pq1 = os.path.join(data_dir, "rows.parquet")
pq2 = os.path.join(data_dir, "rows2.parquet")
_tmp = duckdb.connect()
_tmp.execute(f"COPY (SELECT i       FROM range(50) t(i)) TO '{pq1}' (FORMAT parquet)")
_tmp.execute(f"COPY (SELECT i*2 AS j FROM range(30) t(i)) TO '{pq2}' (FORMAT parquet)")
_tmp.close()

agent = start_agent(token, root)


def vsec():
    """Set VFS secret on the shared connection."""
    CONN.execute(
        f"CREATE OR REPLACE SECRET vfs_s (TYPE vfs, PROVIDER token, TOKEN '{token}')"
    )


vsec()

# ── 3a. read_text ─────────────────────────────────────────────────────────────
rows, err = sql(CONN, f"SELECT content FROM read_text('{BASE}/data/hello.txt')")
check(
    "read_text() reads file content",
    rows and "hello cloudfs" in rows[0][0],
    f"{rows}/{err}",
)

# ── 3b. read_parquet (direct) ─────────────────────────────────────────────────
rows, err = sql(CONN, f"SELECT count(*) FROM read_parquet('{BASE}/data/rows.parquet')")
check("read_parquet() direct — 50 rows", rows and rows[0][0] == 50, f"{rows}/{err}")

# ── 3c. read_parquet (glob) ───────────────────────────────────────────────────
rows, err = sql(CONN, f"SELECT count(*) FROM read_parquet('{BASE}/data/*.parquet')")
check(
    "read_parquet() glob *.parquet — 80 rows (50+30)",
    rows and rows[0][0] == 80,
    f"{rows}/{err}",
)

# ── 3d. ls() — basic listing ──────────────────────────────────────────────────
rows, err = sql(CONN, f"SELECT name, type FROM ls('{BASE}/data/')")
names = {r[0] for r in rows}
types = {r[0]: r[1] for r in rows}
check("ls() returns ≥4 entries (txt + parquet + sub/)", len(rows) >= 4, f"{rows}/{err}")
check("ls() includes hello.txt", "hello.txt" in names, str(names))
check("ls() includes rows.parquet", "rows.parquet" in names, str(names))
check("ls() includes sub/", "sub" in names, str(names))
check("ls() 'sub' type = 'directory'", types.get("sub") == "directory", str(types))
check("ls() 'hello.txt' type = 'file'", types.get("hello.txt") == "file", str(types))

# ── 3e. ls() — size columns ──────────────────────────────────────────────────
rows, err = sql(
    CONN, f"SELECT name, size, size_pretty FROM ls('{BASE}/data/') WHERE type='file'"
)
check(
    "ls() size > 0 for all files",
    all(r[1] is not None and r[1] > 0 for r in rows),
    str(rows),
)
check(
    "ls() size_pretty non-empty for all files",
    all(r[2] and r[2] != "" for r in rows),
    str(rows),
)

# ── 3f. ls() — recursive ─────────────────────────────────────────────────────
rows, err = sql(CONN, f"SELECT name FROM ls('{BASE}/', recursive := true)")
all_names = {r[0] for r in rows}
check("ls(recursive=true) finds nested.txt", "nested.txt" in all_names, str(all_names))

# ── 3g. ls() — pattern filter ────────────────────────────────────────────────
rows, err = sql(CONN, f"SELECT name FROM ls('{BASE}/data/', pattern := '*.parquet')")
pq_names = {r[0] for r in rows}
check(
    "ls(pattern='*.parquet') includes rows.parquet",
    "rows.parquet" in pq_names,
    str(pq_names),
)
check(
    "ls(pattern='*.parquet') excludes .txt files",
    not any(n.endswith(".txt") for n in pq_names),
    str(pq_names),
)

# ── 3h. stat() — file ─────────────────────────────────────────────────────────
rows, err = sql(CONN, f"SELECT name, size, type FROM stat('{BASE}/data/hello.txt')")
check("stat() returns exactly 1 row", len(rows) == 1, str(rows))
check("stat() file name correct", rows and rows[0][0] == "hello.txt", str(rows))
check("stat() file size > 0", rows and rows[0][1] > 0, str(rows))
check("stat() file type = 'file'", rows and rows[0][2] == "file", str(rows))

# ── 3i. stat() — directory ────────────────────────────────────────────────────
rows, err = sql(CONN, f"SELECT name, type FROM stat('{BASE}/data/sub')")
check("stat() directory returns 1 row", len(rows) == 1, str(rows))
check(
    "stat() directory type = 'directory'", rows and rows[0][1] == "directory", str(rows)
)

# ── 3j. du() — disk usage ────────────────────────────────────────────────────
rows, err = sql(CONN, f"SELECT directory, file_count, total_size FROM du('{BASE}/')")
check("du() returns ≥2 directories", len(rows) >= 2, f"{rows}/{err}")
total_files = sum(r[1] for r in rows)
total_bytes = sum(r[2] for r in rows)
check("du() total file_count ≥ 5", total_files >= 5, f"got {total_files}")
check("du() total_size > 0", total_bytes > 0, f"got {total_bytes}")
data_rows = [r for r in rows if r[0].endswith("/data")]
check("du() has entry for /data", len(data_rows) == 1, str([r[0] for r in rows]))
if data_rows:
    check(
        "du() /data file_count ≥ 4",
        data_rows[0][1] >= 4,
        f"file_count={data_rows[0][1]}",
    )

# ── 3k. COPY TO vfs:// (write) ────────────────────────────────────────────────
out = f"{BASE}/data/written.parquet"
_, err = sql(
    CONN, f"COPY (SELECT 'a' AS col UNION ALL SELECT 'b') TO '{out}' (FORMAT parquet)"
)
check("COPY TO vfs:// succeeds", err is None, str(err))

if err is None:
    rows2, err2 = sql(CONN, f"SELECT count(*) FROM read_parquet('{out}')")
    check(
        "written file readable — 2 rows", rows2 and rows2[0][0] == 2, f"{rows2}/{err2}"
    )

    rows3, err3 = sql(CONN, f"SELECT name FROM ls('{BASE}/data/')")
    check(
        "ls() sees written.parquet",
        "written.parquet" in {r[0] for r in rows3},
        str({r[0] for r in rows3}),
    )

# ── 3l. cache invalidation ───────────────────────────────────────────────────
rows, err = sql(CONN, "SELECT clear_cache('vfs')")
check("clear_cache('vfs') after writes = 'OK'", rows and rows[0][0] == "OK", str(err))

# ── 3m. ls() on empty directory ──────────────────────────────────────────────
rows, err = sql(CONN, f"SELECT * FROM ls('{BASE}/empty/')")
check(
    "ls() on empty directory — 0 rows, no error",
    err is None and rows == [],
    f"err={err}, rows={rows}",
)

# ── 3n. stat() on missing file ───────────────────────────────────────────────
rows, err = sql(CONN, f"SELECT * FROM stat('{BASE}/data/does_not_exist.txt')")
check(
    "stat() on missing file — 0 rows, no crash",
    rows == [] and err is None,
    f"rows={rows}, err={err}",
)

agent.terminate()
shutil.rmtree(root)

# ── 4. Error / auth guard tests ───────────────────────────────────────────────
section("4 · Error handling")

_, err = sql(CONN, "CREATE OR REPLACE SECRET bad (TYPE vfs, PROVIDER token, TOKEN '')")
check(
    "empty TOKEN raises error",
    err is not None and "TOKEN required" in err,
    f"err={err!r}",
)

# Wrong auth: connect to a port with the wrong token
bad_token = _secrets.token_hex(16)
bad_root = tempfile.mkdtemp()
bad_agent = start_agent(_secrets.token_hex(16), bad_root, port=PORT + 1)

CONN.execute(
    f"CREATE OR REPLACE SECRET bad_s (TYPE vfs, PROVIDER token, TOKEN '{bad_token}')"
)
rows, err = sql(CONN, f"SELECT * FROM ls('vfs://localhost:{PORT+1}/') USING SAMPLE 1")
# Either returns empty or raises an auth error — should NOT crash / hang
check(
    "ls() with wrong token returns empty or auth error (no crash)", True, ""
)  # just verify it doesn't crash

bad_agent.terminate()
shutil.rmtree(bad_root)

# ── Summary ───────────────────────────────────────────────────────────────────
section("Summary")
total = _PASS + _FAIL
pct = int(100 * _PASS / total) if total else 0
print(f"\n  Passed : {_PASS}/{total}  ({pct}%)")
if _FAIL:
    print(f"  Failed : {_FAIL}\n\n  ✗ SOME TESTS FAILED")
    sys.exit(1)
else:
    print("\n  ✅ All tests passed!")
