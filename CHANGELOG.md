# Changelog

All notable changes to CloudFS are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased] - v0.2.0

Planned work is tracked in the
[v0.2.0 milestone](https://github.com/trouchet/cloudfs/milestone/1).

### Added

- `client_credentials` secret provider for SharePoint and OneDrive (app-only
  authentication for headless use).
- Google Drive `service_account` authentication (RS256-signed JWT).
- `MoveFile` and server-side `CopyItem` across providers.
- `AbortUpload` for all providers: failed uploads release their server-side
  session.
- Native recursive listing for Google Drive; breadth-first recursive listing and
  `CopyItem` for SFTP.
- OAuth refresh tokens are saved in persistent `oauth` secrets, so later
  sessions do not prompt again. `PERSIST_TOKEN false` opts out.
- `cloudfs-agent`: recursive listing, `/v1/move` and `/v1/upload/abort`.
- Integration tests against a real `cloudfs-agent` and unit tests, both run in
  CI.
- [Getting Credentials](docs/CREDENTIALS.md) guide.

### Changed

- **Secrets are resolved through DuckDB's secret manager on every access.**
  Persistent secrets work after a restart, `DROP SECRET` revokes access
  immediately and `SCOPE` selects between several secrets of one type. Sensitive
  options are redacted in `duckdb_secrets()`.
- **Uploads are streamed** in chunks instead of being buffered in memory.
  OneDrive and SharePoint uploads are spooled to a temporary file first, because
  Microsoft Graph needs the total size up front. Writes must be sequential.
- The missing-credentials error is now
  `cloudfs: no secret matches '<url>'. Run: CREATE SECRET (TYPE <type>, ...)`.
- `du()` reports cumulative totals for each directory.
- API responses are parsed with yyjson instead of substring search.
- **Google Drive `PROVIDER oauth` uses the desktop-app flow** (loopback redirect
  with PKCE) instead of the device flow, which Google does not allow for the
  Drive scope. It needs an OAuth client of type "Desktop app"; `REDIRECT_PORT`
  optionally fixes the port of the local listener.
- Several databases in one process can load the extension independently.
- README rewritten to match the current build, secret providers and URL formats.

### Fixed

- HTTP requests that fail at the transport level (DNS, TCP, TLS, timeout) now
  report the curl error instead of a bare status `0`, and are retried when the
  failure is transient.
- Authentication and network errors during glob expansion and listing are
  reported as such instead of "No files found".
- File and folder names containing quotes, spaces, `#`, `%`, `&` or non-ASCII
  characters are escaped in requests and decoded in responses.
- Recursive globs, `ls(recursive := true)` and `du()` returned incomplete
  results through the VFS backend.
- Google Drive: folders with more than 1000 entries could not be listed; service
  account keys could not be loaded.
- Writes to OneDrive, SharePoint, Google Drive and Dropbox sent malformed upload
  requests (not yet verified against the live services).
- Overwriting an existing file through the VFS backend failed.
- The default client ID for Microsoft interactive login was not a registered
  application.

## v0.1.0

Initial version: SharePoint, OneDrive, Google Drive, Dropbox, SFTP and VFS
backends; `ls()`, `stat()` and `du()` table functions; metadata cache.
