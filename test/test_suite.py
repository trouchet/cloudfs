#!/usr/bin/env python3
"""
cloudfs integration tests.

Exercises the extension end to end through the VFS backend, the only provider
that can be tested without cloud credentials: a real cloudfs-agent serves a
temporary directory and DuckDB reads, lists and writes through vfs:// URLs.

Run:
    make release
    python3 test/test_suite.py            # all tests
    python3 test/test_suite.py -k upload  # a subset (unittest options apply)

Requirements:
    - the DuckDB Python package, same version as the extension was built for
    - Go, unless CLOUDFS_AGENT points at a prebuilt cloudfs-agent

Environment:
    CLOUDFS_EXTENSION   path to cloudfs.duckdb_extension
                        (default: build/release/extension/cloudfs/...)
    CLOUDFS_AGENT       path to a cloudfs-agent binary
                        (default: built from agent/ into a temporary directory)
"""

import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import unittest

import duckdb

PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOKEN = "integration-test-token"  # pragma: allowlist secret


def find_extension():
    candidates = [
        os.environ.get("CLOUDFS_EXTENSION"),
        os.path.join(PROJ, "build", "release", "extension", "cloudfs", "cloudfs.duckdb_extension"),
        os.path.join(PROJ, "build", "debug", "extension", "cloudfs", "cloudfs.duckdb_extension"),
        os.path.join(PROJ, "cloudfs.duckdb_extension"),
    ]
    for path in candidates:
        if path and os.path.exists(path):
            return path
    sys.exit("cloudfs.duckdb_extension not found: run `make release` or set CLOUDFS_EXTENSION")


def find_agent(workdir):
    agent = os.environ.get("CLOUDFS_AGENT")
    if agent:
        if not os.path.exists(agent):
            sys.exit(f"CLOUDFS_AGENT does not exist: {agent}")
        return agent
    agent = os.path.join(workdir, "cloudfs-agent")
    try:
        subprocess.run(["go", "build", "-o", agent, "."], cwd=os.path.join(PROJ, "agent"), check=True)
    except (OSError, subprocess.CalledProcessError) as e:
        sys.exit(f"could not build cloudfs-agent ({e}); install Go or set CLOUDFS_AGENT")
    return agent


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Agent:
    """A cloudfs-agent process serving `root` on a free local port."""

    def __init__(self, binary, root, token=TOKEN, extra_args=()):
        self.root = root
        self.port = free_port()
        self.base = f"vfs://127.0.0.1:{self.port}"
        self.proc = subprocess.Popen(
            [binary, "--token", token, "--port", str(self.port), "--root", root, *extra_args],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        deadline = time.time() + 10
        while time.time() < deadline:
            try:
                socket.create_connection(("127.0.0.1", self.port), timeout=0.2).close()
                return
            except OSError:
                if self.proc.poll() is not None:
                    raise RuntimeError("cloudfs-agent exited during startup")
                time.sleep(0.05)
        raise RuntimeError("cloudfs-agent did not start listening")

    def stop(self):
        self.proc.terminate()
        self.proc.wait(timeout=10)


def connect(config=None):
    cfg = {"allow_unsigned_extensions": "true"}
    cfg.update(config or {})
    con = duckdb.connect(config=cfg)
    con.execute(f"LOAD '{EXTENSION}'")
    return con


def write_file(root, rel, content):
    path = os.path.join(root, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    mode = "wb" if isinstance(content, bytes) else "w"
    with open(path, mode) as f:
        f.write(content)


WORKDIR = tempfile.mkdtemp(prefix="cloudfs-test-")
EXTENSION = find_extension()
AGENT_BIN = find_agent(WORKDIR)


class CloudfsTest(unittest.TestCase):
    """Base class: one agent over a fresh tree, one connection with a secret."""

    @classmethod
    def setUpClass(cls):
        cls.root = tempfile.mkdtemp(dir=WORKDIR)
        cls.populate(cls.root)
        cls.agent = Agent(AGENT_BIN, cls.root)
        cls.base = cls.agent.base
        cls.con = connect()
        cls.con.execute(f"CREATE SECRET vfs_main (TYPE vfs, TOKEN '{TOKEN}')")

    @classmethod
    def tearDownClass(cls):
        cls.con.close()
        cls.agent.stop()

    @classmethod
    def populate(cls, root):
        pass

    def q(self, sql):
        return self.con.execute(sql).fetchall()

    def assertFails(self, sql, *fragments, con=None):
        """The statement must fail and its message contain every fragment."""
        with self.assertRaises(duckdb.Error) as ctx:
            (con or self.con).execute(sql).fetchall()
        message = str(ctx.exception)
        for fragment in fragments:
            self.assertIn(fragment, message)
        return message


class BasicsTest(unittest.TestCase):
    def setUp(self):
        self.con = connect()

    def tearDown(self):
        self.con.close()

    def test_version_and_providers(self):
        self.assertIn("cloudfs", self.con.execute("SELECT cloudfs_version()").fetchone()[0])
        providers = self.con.execute("SELECT cloudfs_providers()").fetchone()[0]
        for scheme in ["spfs", "odfs", "gdfs", "dbxfs", "sftp", "vfs"]:
            self.assertIn(scheme, providers)

    def test_clear_cache(self):
        self.assertIn("OK", self.con.execute("SELECT cloudfs_clear_cache()").fetchone()[0])
        self.assertIn("OK", self.con.execute("SELECT cloudfs_clear_cache('vfs')").fetchone()[0])

    def test_every_secret_provider_can_be_created(self):
        cases = [
            ("sharepoint", "oauth", ""),
            ("sharepoint", "token", "TOKEN 'x'"),
            ("sharepoint", "client_credentials", "TENANT_ID 't', CLIENT_ID 'c', CLIENT_SECRET 's'"),
            ("onedrive", "oauth", ""),
            ("onedrive", "token", "TOKEN 'x'"),
            ("onedrive", "client_credentials", "TENANT_ID 't', CLIENT_ID 'c', CLIENT_SECRET 's'"),
            ("gdrive", "oauth", "CLIENT_ID 'c', CLIENT_SECRET 's'"),
            ("gdrive", "service_account", "KEY_JSON '{\"client_email\":\"a@b\"}'"),
            ("gdrive", "token", "TOKEN 'x'"),
            ("dropbox", "oauth", "APP_KEY 'k', APP_SECRET 's'"),
            ("dropbox", "token", "TOKEN 'x'"),
            ("sftp", "keyfile", "KEY_PATH '/nonexistent/id_rsa'"),
            ("sftp", "agent", ""),
            ("sftp", "password", "PASSWORD 'p'"),
            ("vfs", "token", "TOKEN 'x'"),
        ]
        for type_, provider, options in cases:
            with self.subTest(type=type_, provider=provider):
                opts = f", {options}" if options else ""
                self.con.execute(f"CREATE OR REPLACE SECRET s (TYPE {type_}, PROVIDER {provider}{opts})")
                row = self.con.execute("SELECT type, provider FROM duckdb_secrets() WHERE name = 's'").fetchone()
                self.assertEqual(row, (type_, provider))

    def test_secret_validation_and_redaction(self):
        with self.assertRaises(duckdb.Error) as ctx:
            self.con.execute("CREATE SECRET bad (TYPE vfs, TOKEN '')")
        self.assertIn("TOKEN required", str(ctx.exception))
        self.con.execute("CREATE SECRET red (TYPE vfs, TOKEN 'do-not-show-this')")
        shown = self.con.execute("SELECT secret_string FROM duckdb_secrets() WHERE name = 'red'").fetchone()[0]
        self.assertNotIn("do-not-show-this", shown)


class ReadTest(CloudfsTest):
    @classmethod
    def populate(cls, root):
        write_file(root, "data/hello.txt", "hello cloudfs\n")
        write_file(root, "data/a.csv", "n\n1\n2\n")
        write_file(root, "data/sub/b.csv", "n\n10\n")
        write_file(root, "data/sub/deep/c.csv", "n\n100\n")
        write_file(root, "data/sub/notes.txt", "x")
        os.makedirs(os.path.join(root, "data", "empty"))
        local = duckdb.connect()
        for name, rows in [("rows.parquet", 50), ("rows2.parquet", 30)]:
            path = os.path.join(root, "data", name)
            local.execute(f"COPY (SELECT i FROM range({rows}) t(i)) TO '{path}' (FORMAT parquet)")
        # A folder larger than one agent page (1000 entries).
        for i in range(1205):
            write_file(root, f"many/f{i:04d}.csv", f"n\n{i}\n")
        local.close()

    def test_read_text(self):
        self.assertEqual(self.q(f"SELECT content FROM read_text('{self.base}/data/hello.txt')"), [("hello cloudfs\n",)])

    def test_read_csv_and_parquet(self):
        self.assertEqual(self.q(f"SELECT sum(n) FROM read_csv('{self.base}/data/a.csv')"), [(3,)])
        self.assertEqual(self.q(f"SELECT count(*) FROM '{self.base}/data/rows.parquet'"), [(50,)])

    def test_glob(self):
        self.assertEqual(self.q(f"SELECT count(*) FROM read_parquet('{self.base}/data/*.parquet')"), [(80,)])

    def test_glob_recursive_reaches_nested_folders(self):
        self.assertEqual(self.q(f"SELECT sum(n) FROM read_csv('{self.base}/data/**/*.csv')"), [(113,)])
        files = self.q(f"SELECT file FROM glob('{self.base}/data/**/*.csv') ORDER BY 1")
        self.assertEqual(
            [f[0].replace(self.base, "") for f in files],
            ["/data/a.csv", "/data/sub/b.csv", "/data/sub/deep/c.csv"],
        )

    def test_glob_paginated_folder(self):
        rows = self.q(f"SELECT count(*), sum(n) FROM read_csv('{self.base}/many/*.csv')")
        self.assertEqual(rows, [(1205, sum(range(1205)))])

    def test_glob_without_match_is_not_an_access_error(self):
        self.assertFails(f"SELECT * FROM read_csv('{self.base}/data/*.nope')", "No files found")
        self.assertFails(f"SELECT * FROM read_csv('{self.base}/missing/*.csv')", "No files found")

    def test_ls(self):
        rows = dict(self.q(f"SELECT name, type FROM ls('{self.base}/data/')"))
        self.assertEqual(
            rows,
            {
                "hello.txt": "file",
                "a.csv": "file",
                "rows.parquet": "file",
                "rows2.parquet": "file",
                "sub": "directory",
                "empty": "directory",
            },
        )
        sizes = self.q(f"SELECT size, size_pretty FROM ls('{self.base}/data/') WHERE name = 'hello.txt'")
        self.assertEqual(sizes[0][0], len("hello cloudfs\n"))
        self.assertTrue(sizes[0][1])

    def test_ls_recursive_lists_everything_once(self):
        rows = self.q(f"SELECT url, type FROM ls('{self.base}/data/', recursive := true)")
        urls = [r[0].replace(self.base, "") for r in rows]
        self.assertEqual(len(urls), len(set(urls)), "duplicate rows")
        self.assertEqual(
            sorted(urls),
            sorted(
                [
                    "/data/hello.txt",
                    "/data/a.csv",
                    "/data/rows.parquet",
                    "/data/rows2.parquet",
                    "/data/empty",
                    "/data/sub",
                    "/data/sub/b.csv",
                    "/data/sub/notes.txt",
                    "/data/sub/deep",
                    "/data/sub/deep/c.csv",
                ]
            ),
        )

    def test_ls_pattern(self):
        names = {
            r[0]
            for r in self.q(f"SELECT name FROM ls('{self.base}/data/', pattern := '*.parquet') WHERE type = 'file'")
        }
        self.assertEqual(names, {"rows.parquet", "rows2.parquet"})

    def test_ls_paginated_and_empty_folders(self):
        self.assertEqual(self.q(f"SELECT count(*) FROM ls('{self.base}/many/')"), [(1205,)])
        self.assertEqual(self.q(f"SELECT count(*) FROM ls('{self.base}/data/empty/')"), [(0,)])

    def test_stat(self):
        self.assertEqual(
            self.q(f"SELECT name, type, size FROM stat('{self.base}/data/hello.txt')"),
            [("hello.txt", "file", len("hello cloudfs\n"))],
        )
        self.assertEqual(self.q(f"SELECT type FROM stat('{self.base}/data/sub')"), [("directory",)])
        self.assertFails(f"SELECT * FROM stat('{self.base}/data/nope.txt')", "not found")

    def test_du_rolls_up_subdirectories(self):
        rows = self.q(f"SELECT directory, file_count, total_size FROM du('{self.base}/data/')")
        by_dir = {r[0].replace(self.base, ""): (r[1], r[2]) for r in rows}
        self.assertEqual(set(by_dir), {"/data", "/data/empty", "/data/sub", "/data/sub/deep"})
        self.assertEqual(by_dir["/data/empty"], (0, 0))
        self.assertEqual(by_dir["/data/sub/deep"], (1, len("n\n100\n")))
        self.assertEqual(by_dir["/data/sub"], (3, len("n\n100\n") + len("n\n10\n") + 1))
        expected_files, expected_bytes = 0, 0
        for dirpath, _, filenames in os.walk(os.path.join(self.root, "data")):
            for name in filenames:
                expected_files += 1
                expected_bytes += os.path.getsize(os.path.join(dirpath, name))
        self.assertEqual(by_dir["/data"], (expected_files, expected_bytes))


class NamesTest(CloudfsTest):
    """File names with characters that need escaping in URLs and JSON."""

    NAMES = ["it's 50%#&.csv", 'q"uote.csv', "ação ñ.csv", "a}b]{.csv", "plus+equals=.csv"]

    @classmethod
    def populate(cls, root):
        for i, name in enumerate(cls.NAMES):
            write_file(root, f"my dir/{name}", f"n\n{i + 1}\n")

    def test_list_and_read(self):
        names = {r[0] for r in self.q(f"SELECT name FROM ls('{self.base}/my dir/')")}
        self.assertEqual(names, set(self.NAMES))
        total = self.q(f"SELECT sum(n) FROM read_csv('{self.base}/my dir/*.csv')")
        self.assertEqual(total, [(sum(range(1, len(self.NAMES) + 1)),)])
        for i, name in enumerate(self.NAMES):
            with self.subTest(name=name):
                url = f"{self.base}/my dir/{name}".replace("'", "''")
                self.assertEqual(self.q(f"SELECT n FROM read_csv('{url}')"), [(i + 1,)])

    def test_write(self):
        name = 'new {"file"} & ñ].csv'
        url = f"{self.base}/my dir/{name}"
        self.q(f"COPY (SELECT 7 AS n) TO '{url}'")
        self.assertEqual(self.q(f"SELECT n FROM read_csv('{url}')"), [(7,)])
        self.assertTrue(os.path.exists(os.path.join(self.root, "my dir", name)))


class WriteTest(CloudfsTest):
    @classmethod
    def populate(cls, root):
        os.makedirs(os.path.join(root, "out"))

    def test_csv_and_parquet_round_trip(self):
        self.q(f"COPY (SELECT 'a' AS c UNION ALL SELECT 'b') TO '{self.base}/out/small.parquet' (FORMAT parquet)")
        self.assertEqual(self.q(f"SELECT count(*) FROM '{self.base}/out/small.parquet'"), [(2,)])
        self.q(f"COPY (SELECT 42 AS n) TO '{self.base}/out/small.csv'")
        self.assertEqual(self.q(f"SELECT n FROM read_csv('{self.base}/out/small.csv')"), [(42,)])
        names = {r[0] for r in self.q(f"SELECT name FROM ls('{self.base}/out/')")}
        self.assertLessEqual({"small.parquet", "small.csv"}, names)

    def test_upload_spanning_many_chunks(self):
        # ~39 MB of CSV: several 8 MiB chunks, so the streaming path is used.
        rows = 300_000
        self.q(f"COPY (SELECT i AS id, repeat('x', 100) AS pad FROM range({rows}) t(i)) TO '{self.base}/out/big.csv'")
        size = os.path.getsize(os.path.join(self.root, "out", "big.csv"))
        self.assertGreater(size, 3 * 8 * 1024 * 1024)
        self.assertEqual(self.q(f"SELECT size FROM stat('{self.base}/out/big.csv')"), [(size,)])
        self.assertEqual(
            self.q(f"SELECT count(*), sum(id) FROM read_csv('{self.base}/out/big.csv')"),
            [(rows, rows * (rows - 1) // 2)],
        )

    def test_empty_file(self):
        self.q(f"COPY (SELECT 1 AS n WHERE false) TO '{self.base}/out/empty.csv' (HEADER false)")
        self.assertEqual(os.path.getsize(os.path.join(self.root, "out", "empty.csv")), 0)

    def test_overwrite_is_visible(self):
        url = f"{self.base}/out/over.csv"
        self.q(f"COPY (SELECT 1 AS n) TO '{url}'")
        self.assertEqual(self.q(f"SELECT n FROM read_csv('{url}')"), [(1,)])
        self.q(f"COPY (SELECT 2 AS n UNION ALL SELECT 3) TO '{url}'")
        self.assertEqual(self.q(f"SELECT sum(n) FROM read_csv('{url}')"), [(5,)])

    def test_missing_parent_folder(self):
        self.assertFails(f"COPY (SELECT 1 AS n) TO '{self.base}/no/such/dir/x.csv'", "parent not found")


class UploadLimitTest(unittest.TestCase):
    def test_rejected_upload_leaves_no_partial_file(self):
        root = tempfile.mkdtemp(dir=WORKDIR)
        os.makedirs(os.path.join(root, "out"))
        agent = Agent(AGENT_BIN, root, extra_args=["--max-upload-mb", "10"])
        con = connect()
        try:
            con.execute(f"CREATE SECRET v (TYPE vfs, TOKEN '{TOKEN}')")
            with self.assertRaises(duckdb.Error) as ctx:
                con.execute(
                    f"COPY (SELECT i AS id, repeat('x', 100) AS pad FROM range(300000) t(i)) "
                    f"TO '{agent.base}/out/toobig.csv'"
                )
            self.assertIn("413", str(ctx.exception))
            self.assertEqual(os.listdir(os.path.join(root, "out")), [])
            con.execute(f"COPY (SELECT 1 AS n) TO '{agent.base}/out/ok.csv'")
            self.assertEqual(os.listdir(os.path.join(root, "out")), ["ok.csv"])
        finally:
            con.close()
            agent.stop()


class ErrorsTest(CloudfsTest):
    @classmethod
    def populate(cls, root):
        write_file(root, "data/a.csv", "n\n1\n")

    def test_missing_secret_is_reported(self):
        con = connect()
        try:
            for sql in [
                f"SELECT * FROM read_csv('{self.base}/data/a.csv')",
                f"SELECT * FROM read_csv('{self.base}/data/*.csv')",
                f"SELECT * FROM ls('{self.base}/data/')",
            ]:
                with self.subTest(sql=sql):
                    self.assertFails(sql, "no secret matches", "TYPE vfs", con=con)
        finally:
            con.close()

    def test_wrong_token_is_reported(self):
        con = connect()
        try:
            con.execute("CREATE SECRET wrong (TYPE vfs, TOKEN 'not-the-token')")
            self.assertFails(f"SELECT * FROM read_csv('{self.base}/data/*.csv')", "403", con=con)
            self.assertFails(f"SELECT * FROM ls('{self.base}/data/')", "403", con=con)
        finally:
            con.close()

    def test_unreachable_agent_is_reported(self):
        dead = f"vfs://127.0.0.1:{free_port()}"
        message = self.assertFails(f"SELECT * FROM read_csv('{dead}/data/*.csv')", "cannot expand")
        self.assertNotIn("No files found", message)


class SecretLifecycleTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.agents = []
        for token, value in [("token-a", 1), ("token-b", 2)]:
            root = tempfile.mkdtemp(dir=WORKDIR)
            write_file(root, "data/v.csv", f"n\n{value}\n")
            cls.agents.append(Agent(AGENT_BIN, root, token=token))

    @classmethod
    def tearDownClass(cls):
        for agent in cls.agents:
            agent.stop()

    def read(self, con, agent):
        return con.execute(f"SELECT n FROM read_csv('{agent.base}/data/v.csv')").fetchall()

    def test_scope_selects_the_secret(self):
        a, b = self.agents
        con = connect()
        try:
            con.execute(f"CREATE SECRET sa (TYPE vfs, TOKEN 'token-a', SCOPE '{a.base}')")
            con.execute(f"CREATE SECRET sb (TYPE vfs, TOKEN 'token-b', SCOPE '{b.base}')")
            self.assertEqual(self.read(con, a), [(1,)])
            self.assertEqual(self.read(con, b), [(2,)])
        finally:
            con.close()

    def test_drop_and_replace(self):
        a, _ = self.agents
        con = connect()
        try:
            con.execute("CREATE SECRET s (TYPE vfs, TOKEN 'wrong')")
            with self.assertRaises(duckdb.Error):
                self.read(con, a)
            con.execute("CREATE OR REPLACE SECRET s (TYPE vfs, TOKEN 'token-a')")
            self.assertEqual(self.read(con, a), [(1,)])
            con.execute("DROP SECRET s")
            with self.assertRaises(duckdb.Error) as ctx:
                self.read(con, a)
            self.assertIn("no secret matches", str(ctx.exception))
        finally:
            con.close()

    def test_persistent_secret_survives_a_new_database(self):
        a, _ = self.agents
        secret_dir = tempfile.mkdtemp(dir=WORKDIR)
        config = {"secret_directory": secret_dir}
        con = connect(config)
        con.execute("CREATE PERSISTENT SECRET p (TYPE vfs, TOKEN 'token-a')")
        con.close()
        con = connect(config)
        try:
            self.assertEqual(self.read(con, a), [(1,)])
        finally:
            con.execute("DROP PERSISTENT SECRET p")
            con.close()

    def test_databases_in_one_process_are_independent(self):
        a, b = self.agents
        con_a, con_b = connect(), connect()
        try:
            con_a.execute("CREATE SECRET s (TYPE vfs, TOKEN 'token-a')")
            with self.assertRaises(duckdb.Error) as ctx:
                self.read(con_b, a)
            self.assertIn("no secret matches", str(ctx.exception))
            con_b.execute("CREATE SECRET s (TYPE vfs, TOKEN 'token-b')")
            self.assertEqual(self.read(con_a, a), [(1,)])
            self.assertEqual(self.read(con_b, b), [(2,)])
        finally:
            con_a.close()
            con_b.close()


if __name__ == "__main__":
    print(f"extension: {EXTENSION}\nagent:     {AGENT_BIN}\nduckdb:    {duckdb.__version__}\n")
    try:
        unittest.main(verbosity=2)
    finally:
        shutil.rmtree(WORKDIR, ignore_errors=True)
