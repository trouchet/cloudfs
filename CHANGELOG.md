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

### Fixed

- HTTP requests that fail at the transport level (DNS, TCP, TLS, timeout) now
  report the curl error instead of a bare status `0`, and are retried when the
  failure is transient.

### Changed

- README rewritten to match the current build, secret providers and URL formats,
  with a list of known limitations.

## v0.1.0

Initial version: SharePoint, OneDrive, Google Drive, Dropbox, SFTP and VFS
backends; `ls()`, `stat()` and `du()` table functions; metadata cache.
