# CloudFS Authentication Implementation Roadmap

## 📌 Overview

This document outlines the systematic fix for CloudFS authentication gaps across all providers.

**Goal**: Enable server-to-server authentication across all major cloud providers.

**Timeline**: 3 development sessions

---

## 🎯 Priority Tier 1: Critical Blockers (Session 1-2)

### Tier 1.1: SharePoint Client Credentials

**Status**: ✅ Branch ready → `feat/sharepoint-client-credentials`

**What's Missing**: C++ implementation of Client Credentials grant

**Required Changes**:
1. Add parameter parsing for `CLIENT_SECRET` in SECRET creation
2. Implement `SharePointClientCredentialsAuth` class
3. Token caching + refresh logic
4. Error handling

**Files to Modify**:
- `src/providers/sharepoint/sharepoint_auth.cpp` - Main implementation
- `src/providers/sharepoint/sharepoint_auth.hpp` - Add new class
- Tests + documentation

**Estimated Effort**: 60-90 minutes

**Testing**:
```bash
export CLOUDFS_SHAREPOINT_CLIENT_ID="<your-client-id>"
export CLOUDFS_SHAREPOINT_CLIENT_SECRET="<your-client-secret>"
export CLOUDFS_SHAREPOINT_TENANT_ID="<your-tenant-id>"

# Test query:
duckdb-cloudfs << 'EOF'
LOAD cloudfs;
SELECT * FROM ls('spfs://Concilium/prod/') LIMIT 5;
EOF
```

**Merge Checklist**:
- [ ] Code review complete
- [ ] Tests pass (build_and_test.sh)
- [ ] Conventional Commits format
- [ ] Documentation updated
- [ ] No breaking changes to Device Code Flow

---

### Tier 1.2: OneDrive Client Credentials

**Status**: 📋 Not started

**What's Missing**: Same as SharePoint (different endpoint nuance)

**Complexity**: Slightly simpler - almost identical to SharePoint

**Files to Modify**:
- `src/providers/onedrive/onedrive_auth.cpp`
- `src/providers/onedrive/onedrive_auth.hpp`

**Estimated Effort**: 45-60 minutes

**Branch Strategy**:
```bash
git checkout -b feat/onedrive-client-credentials
# Based on: feat/sharepoint-client-credentials patterns
```

---

## 🎯 Priority Tier 2: High Impact (Session 2-3)

### Tier 2.1: Google Drive Service Account JWT

**Status**: ⚠️ Stubbed - JWT code exists but signing broken

**What's Missing**: RSA-SHA256 signing using OpenSSL EVP

**Root Cause**: Line 132 in `gdrive_auth.cpp`
```cpp
// TODO: integrate OpenSSL RSA signing via EVP_DigestSign*
```

**Required Changes**:
1. Parse private_key from service account JSON
2. Extract RSA key using OpenSSL
3. Create JWT header + payload
4. Sign with EVP_DigestSign (SHA256)
5. Exchange JWT for access token

**Files to Modify**:
- `src/providers/gdrive/gdrive_auth.cpp` - Implement JWT signing
- `src/providers/gdrive/gdrive_auth.hpp` - Add ServiceAccountAuth class
- Update CMakeLists.txt if OpenSSL linking needed

**Estimated Effort**: 90-120 minutes

**OpenSSL Integration**:
```cpp
// Pseudocode
EVP_PKEY *pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
EVP_DigestSignInit(mdctx, nullptr, EVP_sha256(), nullptr, pkey);
EVP_DigestSignUpdate(mdctx, (unsigned char *)data, data_len);
EVP_DigestSignFinal(mdctx, sig, sig_len);
```

**Testing**:
```bash
# Create service account JSON from Google Cloud Console
export GOOGLE_DRIVE_SERVICE_ACCOUNT=$(cat service-account.json | base64)

duckdb-cloudfs << 'EOF'
LOAD cloudfs;
SELECT * FROM ls('gdfs://My Shared Drive/');
EOF
```

**Dependencies**: OpenSSL dev (likely already available)

---

### Tier 2.2: Dropbox True Device Code Flow

**Status**: ⚠️ Partial - Manual code paste (not headless)

**What's Missing**: True RFC 8628 Device Code Flow

**Research Needed**: 
- Does Dropbox API support RFC 8628?
- If yes: implement device_code grant
- If no: implement client_credentials as fallback

**Branch Strategy**:
```bash
git checkout -b feat/dropbox-device-code-flow
# Research first, then implement
```

**Estimated Effort**: 60-90 minutes (research + implementation)

---

## 📊 Implementation Schedule

```
Session 1 (Today/Tomorrow):
├─ SharePoint Client Credentials (Tier 1.1) - 90 min
├─ Code review + merge
└─ Document + publish PR

Session 2 (This week):
├─ OneDrive Client Credentials (Tier 1.2) - 60 min
├─ Google Drive JWT Research (Tier 2.1 prep) - 30 min
└─ Merge + test

Session 3 (Next week):
├─ Google Drive Service Account JWT (Tier 2.1) - 120 min
├─ Dropbox RFC 8628 Research (Tier 2.2) - 30 min
└─ PR creation + review
```

---

## 🛠️ Branch Strategy

All changes use Conventional Commits:

```bash
# Tier 1 fixes
feat(sharepoint): implement client credentials auth
feat(onedrive): implement client credentials auth

# Tier 2 fixes
feat(gdrive): implement service account jwt signing
feat(dropbox): implement rfc 8628 device code flow
```

---

## ✅ Completion Checklist

### Tier 1
- [ ] SharePoint Client Credentials (feat branch created)
  - [ ] Implementation complete
  - [ ] Tests passing
  - [ ] PR created
  - [ ] PR merged to main
- [ ] OneDrive Client Credentials
  - [ ] Implementation complete
  - [ ] Tests passing
  - [ ] PR created
  - [ ] PR merged to main

### Tier 2
- [ ] Google Drive Service Account
  - [ ] Research OpenSSL integration
  - [ ] Implementation complete
  - [ ] Tests passing
  - [ ] PR created
  - [ ] PR merged to main
- [ ] Dropbox Device Code Flow
  - [ ] Research Dropbox API
  - [ ] Implementation complete
  - [ ] Tests passing
  - [ ] PR created
  - [ ] PR merged to main

### Documentation
- [ ] AUTHENTICATION_AUDIT.md complete
- [ ] Provider-specific guides updated
- [ ] Enterprise authentication guide created
- [ ] Migration guide for users (Device Code → Client Credentials)

---

## 📖 Documentation Artifacts

### Create These Files

#### 1. `docs/ENTERPRISE_AUTHENTICATION.md`
- When to use Client Credentials vs Device Code
- Setup guides per provider
- Security best practices
- Troubleshooting

#### 2. `docs/SERVICE_ACCOUNTS_GUIDE.md`
- How to create service accounts
- Required permissions per provider
- JSON setup
- CI/CD integration examples

#### 3. `docs/AUTHENTICATION_MIGRATION.md`
- For existing users: upgrade from Device Code to Client Credentials
- Backward compatibility notes
- Side-by-side examples

---

## 🔐 Security Checklist

For each implementation:

- [ ] Credentials never logged
- [ ] Secrets not in git history
- [ ] Client secrets handled as sensitive
- [ ] Token expiration + refresh logic
- [ ] Error messages don't leak sensitive info
- [ ] Connection strings validated

---

## 🚀 Success Metrics

After all Tiers complete:

| Metric | Before | After |
|--------|--------|-------|
| Providers with server-to-server auth | 2/6 | 6/6 |
| CI/CD compatible providers | 2/6 | 6/6 |
| Enterprise-ready providers | 0/6 | 6/6 |
| Headless support | 3/6 | 6/6 |

---

## 📝 Notes

### Why This Matters
- Users cannot use CloudFS in automation (GitHub Actions, Jenkins, Airflow)
- Enterprise adoption blocked without service accounts
- Current implementation is "developer-friendly" but not "enterprise-ready"

### Why It's Important to Fix Now
- Repository just professionalized
- Community will ask for this feature
- PR contributions will ask "where do I implement Client Credentials?"
- Better to have clear patterns established

### Implementation Philosophy
- Don't remove Device Code Flow (users still need interactive auth)
- Add Client Credentials alongside, not replace
- Reuse same SECRET creation mechanism
- Maintain backward compatibility

---

## 📞 Questions for Review

Before starting implementation:

1. Should Client Credentials be default or opt-in?
   - Recommendation: Opt-in (pass client_secret in SECRET)
2. Token caching strategy?
   - Recommendation: In-memory cache per connection
3. Refresh token needed for Client Credentials?
   - Answer: No (service account tokens don't expire for days)
4. Error messages for missing credentials?
   - Recommendation: "Client Credentials not configured. Create SECRET with client_secret parameter."


