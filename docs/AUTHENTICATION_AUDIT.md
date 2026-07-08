# CloudFS Authentication Audit

## 🔍 Executive Summary

CloudFS providers implementam **Device Code Flow interativo** como método primário. **Nenhum** suporta autenticação server-to-server (Client Credentials ou Service Accounts).

**Impact**: Impossível usar CloudFS em cenários headless (CI/CD, cron jobs, APIs).

| Provider | Status | Blocker |
|----------|--------|---------|
| SharePoint | ❌ Device Code only | Client Credentials não implementado |
| OneDrive | ❌ Device Code only | Client Credentials não implementado |
| Google Drive | ⚠️ Device Code + JWT Stub | JWT signing não funciona (sem OpenSSL) |
| Dropbox | ⚠️ Manual Code Paste | Não é Device Code legítimo (PKCE) |
| SFTP | ✅ SSH Keys | OK para headless |
| VFS | ✅ Bearer Token | OK para headless (estático) |

---

## 📊 Detailed Analysis

### 1. OneDrive (odfs://)

**File**: `src/providers/onedrive/onedrive_auth.cpp`

**Auth Methods**:
- ✅ OAuth2 Device Code Flow (RFC 8628)
- ✅ Refresh Token Grant
- ✅ Bearer Token (static)

**Headless Support**: ✅ Parcial (polling works)

**Missing**: ❌ **Client Credentials Flow**

**Grant Types Implemented**:
```
grant_type=device_code
grant_type=refresh_token
```

**Missing Grant Type**:
```
grant_type=client_credentials  ← NOT IMPLEMENTED
```

**Use Case Impact**:
- ✅ CLI with user interaction
- ✅ Docker with device code polling
- ❌ CI/CD pipelines (GitHub Actions, Jenkins)
- ❌ Cron jobs
- ❌ Server-to-server integrations
- ❌ API services

**Required Implementation**:
```cpp
// Missing:
POST /oauth2/v2.0/token
  grant_type=client_credentials
  client_id=<APP_ID>
  client_secret=<APP_SECRET>
  scope=https://graph.microsoft.com/.default
```

---

### 2. SharePoint (spfs://)

**File**: `src/providers/sharepoint/sharepoint_auth.cpp`

**Auth Methods**:
- ✅ OAuth2 Device Code Flow (Microsoft v2.0)
- ✅ Refresh Token Grant
- ✅ Bearer Token (static)

**Headless Support**: ✅ Parcial (polling works)

**Missing**: ❌ **Client Credentials Flow**

**Status**: **IDENTICAL TO OneDrive** - same limitation, different endpoint

**Use Case Impact**: Mesma situação que OneDrive

**Branch Status**: `feat/sharepoint-client-credentials` - Ready for implementation

---

### 3. Google Drive (gdfs://)

**File**: `src/providers/gdrive/gdrive_auth.cpp`

**Auth Methods**:
- ✅ OAuth2 Device Code Flow (RFC 8628)
- ⚠️ Service Account JWT (BROKEN - returns error)
- ✅ Refresh Token Grant
- ✅ Bearer Token (static)

**Headless Support**: ⚠️ Parcial (Device Code yes, Service Account no)

**Missing**: ❌ **OpenSSL RSA-SHA256 JWT Signing**

**Code Status**:
```cpp
// Line 132 - gdrive_auth.cpp
// TODO: integrate OpenSSL RSA signing via EVP_DigestSign*
// Currently returns: "JWT signing not yet implemented in this build"
```

**Error Message**:
```
JWT signing not yet implemented in this build. Use bearer token or device code flow instead.
```

**Use Case Impact**:
- ✅ CLI with device code
- ⚠️ Service accounts (NOT FUNCTIONAL)
- ❌ CI/CD with service accounts (blocked)
- ❌ API services (blocked)

**Root Cause**: JWT requires RSA-SHA256 signature using private key - needs OpenSSL EVP integration

**Implementation Needed**:
```cpp
// Generate JWT with:
// Header: {"alg":"RS256","typ":"JWT"}
// Payload: {"iss":"...","scope":"...","aud":"https://oauth2.googleapis.com/token","exp":...}
// Signature: RSA-SHA256(private_key, header.payload)

// Then POST to:
POST https://oauth2.googleapis.com/token
  grant_type=urn:ietf:params:oauth:grant-type:jwt-bearer
  assertion=<JWT>
```

---

### 4. Dropbox (dbxfs://)

**File**: `src/providers/dropbox/dropbox_auth.cpp`

**Auth Methods**:
- ✅ OAuth2 Authorization Code (PKCE)
- ✅ Refresh Token Grant
- ✅ Bearer Token (static)

**Headless Support**: ❌ NO - Manual code paste required

**Missing**: 
- ❌ **True Device Code Flow**
- ❌ **Client Credentials**

**Flow Details**:

```
1. User visits: https://www.dropbox.com/oauth2/authorize?client_id=...
2. User gets code from callback URL or manually
3. User PASTES code into terminal stdin ← ❌ NOT HEADLESS
4. Client exchanges code for token
```

**Grant Types**:
```
grant_type=authorization_code  (with client_secret in body)
grant_type=refresh_token
```

**Use Case Impact**:
- ✅ CLI (barely - requires manual intervention)
- ❌ Docker/containers (can't send input)
- ❌ CI/CD pipelines
- ❌ Cron jobs
- ❌ API services

**Problem**: This is NOT Device Code Flow (RFC 8628) - it's Authorization Code Flow with manual code input

**Better Solution**: Implement true Device Code Flow if Dropbox supports it, or Client Credentials

---

### 5. SFTP (sftp://)

**File**: `src/providers/sftp/sftp_backend.cpp`

**Auth Methods**:
- ✅ SSH Private Key
- ✅ SSH Agent
- ✅ Password (cleartext, not recommended)

**Headless Support**: ✅ YES

**Format**:
```
keyfile:/home/user/.ssh/id_rsa
keyfile:/home/user/.ssh/id_rsa:passphrase
agent:
password:secret
```

**Status**: ✅ GOOD - Already headless friendly

---

### 6. VFS (vfs://)

**File**: `src/providers/vfs/vfs_backend.cpp`

**Auth Methods**:
- ✅ Bearer Token (static)

**Headless Support**: ✅ YES

**Format**:
```
Bearer token passed via CREATE SECRET
```

**Status**: ✅ OK - Simple static token

---

## 🎯 Priority Fix Matrix

### 🔴 CRITICAL (Blocks Enterprise Use)

#### #1: Google Drive Service Account JWT Signing
- **Impact**: Enterprise unable to use GDrive server-to-server
- **Effort**: Medium (need OpenSSL EVP integration)
- **Timeline**: 2-3 hours
- **File**: `src/providers/gdrive/gdrive_auth.cpp:132`
- **Dependencies**: OpenSSL (likely already in build)

#### #2: SharePoint/OneDrive Client Credentials
- **Impact**: Enterprise unable to use O365 in CI/CD/servers
- **Effort**: Medium (similar structure to Device Code)
- **Timeline**: 1-2 hours per provider (2 total)
- **Files**: 
  - `src/providers/sharepoint/sharepoint_auth.cpp`
  - `src/providers/onedrive/onedrive_auth.cpp`
- **Dependencies**: None (same OAuth2 endpoint)

### 🟡 HIGH (Improves UX)

#### #3: Dropbox True Device Code or Client Credentials
- **Impact**: Better headless UX for Dropbox
- **Effort**: Medium
- **Timeline**: 1-2 hours
- **File**: `src/providers/dropbox/dropbox_auth.cpp`
- **Research**: Check if Dropbox supports RFC 8628 Device Code

---

## 📋 Implementation Checklist

### SharePoint Client Credentials (READY - feat branch exists)

**Branch**: `feat/sharepoint-client-credentials`
**Status**: WIP - awaiting C++ implementation

```cpp
// New class: SharePointClientCredentialsAuth
// Required parameters:
//   - client_id (from CREATE SECRET)
//   - client_secret (from CREATE SECRET)
//   - tenant_id (from CREATE SECRET)
//   - scope (default: https://graph.microsoft.com/.default)

// POST to:
// https://login.microsoftonline.com/{tenant_id}/oauth2/v2.0/token
// grant_type=client_credentials
// client_id={client_id}
// client_secret={client_secret}
// scope=https://graph.microsoft.com/.default

// Returns: access_token (no refresh needed - service account)
```

**Test Parameters** (from ~/.env.sharepoint - USE YOUR OWN VALUES):
```bash
export CLOUDFS_SHAREPOINT_CLIENT_ID="<your-client-id>"
export CLOUDFS_SHAREPOINT_CLIENT_SECRET="<your-client-secret>"
export CLOUDFS_SHAREPOINT_TENANT_ID="<your-tenant-id>"
```

### OneDrive Client Credentials

**Similar to SharePoint** - same OAuth endpoint structure

```cpp
// New class: OneDriveClientCredentialsAuth
// Same parameters as SharePoint
// Same endpoint: https://login.microsoftonline.com/{tenant_id}/oauth2/v2.0/token
```

### Google Drive JWT Signing

**Status**: Stubbed but structure exists

```cpp
// Line 132 - gdrive_auth.cpp
// Need to implement:
//   1. RSA-SHA256 signing using private_key from service account JSON
//   2. JWT creation (header.payload.signature)
//   3. POST to https://oauth2.googleapis.com/token with JWT assertion
```

---

## 🔐 Security Notes

### Client Credentials Considerations
- ✅ Credentials stored in `CREATE SECRET` (DuckDB managed)
- ✅ No credential logs (secrets not printed)
- ✅ Token is short-lived
- ⚠️ client_secret must be protected (never in logs/git)

### Service Account JWT Considerations
- ✅ private_key from service account JSON
- ✅ JWT is signed, verified by provider
- ✅ No refresh tokens needed
- ⚠️ Service account JSON must be protected

---

## 📈 Expected Outcomes

### After Fixes
| Provider | Before | After |
|----------|--------|-------|
| SharePoint | ❌ Interactive only | ✅ Server + Interactive |
| OneDrive | ❌ Interactive only | ✅ Server + Interactive |
| Google Drive | ❌ Broken JWT | ✅ Working Service Account |
| Dropbox | ❌ Manual paste | ✅ True Device Code |
| SFTP | ✅ OK | ✅ No change |
| VFS | ✅ OK | ✅ No change |

### Use Cases Enabled
1. ✅ CI/CD pipelines (GitHub Actions, Jenkins)
2. ✅ Cron jobs (scheduled data sync)
3. ✅ API services (query SharePoint on demand)
4. ✅ Microservices (inter-service data access)
5. ✅ Batch processing (server-side data pipelines)

---

## 🛠️ Next Steps

### Immediate (This Session)
1. [ ] Complete `feat/sharepoint-client-credentials` implementation
2. [ ] Create PR with Client Credentials for SharePoint
3. [ ] Document required environment variables

### Short Term (Next Session)
1. [ ] Implement OneDrive Client Credentials
2. [ ] Research Dropbox Device Code support
3. [ ] Create GDrive JWT signing feature branch

### Long Term (Roadmap)
1. [ ] Add Google Drive Service Account JWT
2. [ ] Improve Dropbox authentication UX
3. [ ] Add API Key support to VFS
4. [ ] Create enterprise authentication guide

---

## 📚 References

- OAuth2 Device Code Flow: [RFC 8628](https://tools.ietf.org/html/rfc8628)
- OAuth2 Client Credentials: [RFC 6749 Section 4.4](https://tools.ietf.org/html/rfc6749#section-4.4)
- OpenSSL EVP: [OpenSSL Documentation](https://www.openssl.org/docs/man1.1.1/man3/EVP_DigestSign.html)
- Google Service Account JWT: [Google Docs](https://developers.google.com/identity/protocols/oauth2/service-account)
- Microsoft Identity Platform: [Microsoft Docs](https://docs.microsoft.com/en-us/azure/active-directory/develop/v2-oauth2-client-creds-grant-flow)
