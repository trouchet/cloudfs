# Getting Credentials

Where each provider's credentials come from, with links to the vendor's own
instructions. Menu names and permission names below are quoted from those pages
as they read in November 2026; vendor consoles change, so follow the link when a
label does not match.

| Provider     | `TYPE`       | `PROVIDER`           | You need                                   |
| ------------ | ------------ | -------------------- | ------------------------------------------ |
| SharePoint   | `sharepoint` | `oauth`              | An app registration (recommended)          |
| SharePoint   | `sharepoint` | `client_credentials` | App registration + client secret + consent |
| OneDrive     | `onedrive`   | `oauth`              | An app registration (recommended)          |
| OneDrive     | `onedrive`   | `client_credentials` | App registration + client secret + consent |
| Google Drive | `gdrive`     | `service_account`    | A service account JSON key                 |
| Google Drive | `gdrive`     | `oauth`              | A "Desktop app" OAuth client ID            |
| Dropbox      | `dropbox`    | `oauth`              | An app key and app secret                  |
| SFTP         | `sftp`       | `keyfile` / `agent`  | An SSH key                                 |
| any of these | …            | `token`              | A bearer token you obtained yourself       |

## Microsoft: SharePoint and OneDrive

Both use Microsoft Graph and the Microsoft identity platform, so the setup is
the same.

### 1. Register an app

Follow
[How to register an app in Microsoft Entra ID](https://learn.microsoft.com/en-us/entra/identity-platform/quickstart-register-app):

1. Sign in to the [Microsoft Entra admin center](https://entra.microsoft.com).
1. Go to **Entra ID > App registrations** and select **New registration**.
1. Enter a name and choose the **Supported account types**.
1. Select **Register**.
1. On the app's **Overview** page, copy the **Application (client) ID**
   (`CLIENT_ID`) and the **Directory (tenant) ID** (`TENANT_ID`).

Other ways to find the tenant ID are in
[How to find your tenant ID](https://learn.microsoft.com/en-us/entra/fundamentals/how-to-find-tenant).

### 2a. Interactive login (`PROVIDER oauth`)

This is the
[OAuth 2.0 device authorization grant](https://learn.microsoft.com/en-us/entra/identity-platform/v2-oauth2-device-code):
the first access prints a URL and a code, and you sign in from any browser.

The device code flow is only available to public client applications, so enable
it on your app
([instructions](https://learn.microsoft.com/en-us/entra/identity-platform/scenario-desktop-app-configuration)):

1. Under **Manage**, select **Authentication**.
1. Under **Advanced settings**, for **Allow public client flows**, select
   **Yes**.
1. Select **Save**.

Then add delegated Microsoft Graph permissions under **API permissions**. Per
the Graph reference for
[listing](https://learn.microsoft.com/en-us/graph/api/driveitem-list-children?view=graph-rest-1.0)
and
[uploading](https://learn.microsoft.com/en-us/graph/api/driveitem-put-content?view=graph-rest-1.0)
files:

| To do | Least privileged delegated permission |
| ----- | ------------------------------------- |
| Read  | `Files.Read`                          |
| Write | `Files.ReadWrite`                     |

`Files.Read.All`, `Files.ReadWrite.All`, `Sites.Read.All` and
`Sites.ReadWrite.All` also work and reach files shared with you.

```sql
CREATE SECRET sp (
    TYPE sharepoint,
    PROVIDER oauth,
    TENANT_ID 'your-tenant-id',
    CLIENT_ID 'your-client-id'
);
```

Notes:

- **Use your own `CLIENT_ID`.** Without one, CloudFS falls back to Microsoft's
  Azure CLI application. Its Graph permissions are not yours to configure and an
  organization can block device code sign-in for it, so it may not work in your
  tenant.
- **Use your tenant ID, or `organizations`, for work and school accounts.** The
  default `TENANT_ID` is `common`.
- A refresh token is issued only when the requested scope includes
  `offline_access`. `onedrive` requests it. `sharepoint` requests
  `https://graph.microsoft.com/.default` unless you pass `SCOPE`; if you are
  asked to sign in again after about an hour, pass
  `SCOPE 'https://graph.microsoft.com/Files.ReadWrite.All offline_access'`.

### 2b. Unattended access (`PROVIDER client_credentials`)

This is the
[OAuth 2.0 client credentials flow](https://learn.microsoft.com/en-us/entra/identity-platform/v2-oauth2-client-creds-grant-flow):
the app signs in as itself, with no user. See also
[Get access without a user](https://learn.microsoft.com/en-us/graph/auth-v2-service).

Create a client secret
([instructions](https://learn.microsoft.com/en-us/entra/identity-platform/how-to-add-credentials)):

1. In **App registrations**, select your application.
1. Select **Certificates & secrets > Client secrets > New client secret**.
1. Add a description, pick an expiration and select **Add**.
1. Copy the secret **Value** now (`CLIENT_SECRET`). It is never shown again.

Grant application permissions:

1. On the **API permissions** page, choose **Add a permission**.
1. Select **Microsoft Graph**, then **Application permissions**.
1. Add the permissions and have an administrator grant consent. Application
   permissions always require administrator consent.

| To do | Least privileged application permission |
| ----- | --------------------------------------- |
| Read  | `Files.Read.All`                        |
| Write | `Files.ReadWrite.All`                   |

`Sites.Read.All` and `Sites.ReadWrite.All` also work. To limit the app to
specific sites, use `Sites.Selected` and assign the app a role on each site as
described in the
[selected permissions overview](https://learn.microsoft.com/en-us/graph/permissions-selected-overview);
consent alone grants nothing with that permission.

```sql
CREATE SECRET sp (
    TYPE sharepoint,
    PROVIDER client_credentials,
    TENANT_ID 'your-tenant-id',
    CLIENT_ID 'your-client-id',
    CLIENT_SECRET 'your-client-secret'
);
```

Notes:

- `TENANT_ID` must be your tenant's ID or domain name; `common` does not apply
  to this flow.
- This flow reaches organizational data only. It does not work for personal
  Microsoft accounts (OneDrive personal); use `oauth` or `token` there.
- Microsoft recommends certificates over client secrets for production. CloudFS
  supports client secrets only.

### 2c. A token you already have (`PROVIDER token`)

Any valid Microsoft Graph access token works. The Azure CLI
[documents](https://learn.microsoft.com/en-us/cli/azure/account?view=azure-cli-latest#az-account-get-access-token)
this command for Graph:

```bash
az account get-access-token --resource-type ms-graph
```

The token is valid for at most 60 minutes and carries whatever Graph permissions
the Azure CLI application has for your account, which may not include file
access in your tenant.

## Google Drive

Enable the Google Drive API in your Google Cloud project first:
[Enable Google Workspace APIs](https://developers.google.com/workspace/guides/enable-apis)
(**APIs & Services > Library**, select the API, click **Enable**).

### Service account (`PROVIDER service_account`)

Follow
[Create access credentials](https://developers.google.com/workspace/guides/create-credentials):

1. In the Google Cloud console, go to **IAM & Admin > Service Accounts** and
   select your project.
1. Click **Create service account**, enter a name, and finish.
1. Note the service account's email address.
1. Open the service account, go to the **Keys** tab, and select **Add key >
   Create new key**.
1. Select **JSON** and click **Create**. The downloaded file is the only copy of
   the key.

Give the service account access to your data by sharing the folder or file with
its email address, exactly as you would share with a person, or by adding it as
a member of a shared drive.

```sql
CREATE SECRET gd (
    TYPE gdrive,
    PROVIDER service_account,
    KEY_JSON '<contents of the downloaded JSON file>'
);
```

Notes:

- **Writing requires a shared drive.** Google states that service accounts
  "don't have storage quota and can't own files. Instead, they must upload files
  and folders into shared drives". A service account can read a folder in
  someone's My Drive that was shared with it, but cannot create files there.
- Domain-wide delegation (impersonating a user) is not supported by CloudFS.

### Interactive login (`PROVIDER oauth`)

CloudFS uses Google's
[OAuth 2.0 flow for desktop apps](https://developers.google.com/identity/protocols/oauth2/native-app)
with a loopback redirect and PKCE: you open a URL in a browser, sign in, and
Google redirects the browser to a short-lived listener on `127.0.0.1` that
CloudFS opened for the occasion.

1. Configure the OAuth consent screen for your project:
   [Configure OAuth consent](https://developers.google.com/workspace/guides/configure-oauth-consent).
1. Create an OAuth client ID as described in
   [Create access credentials](https://developers.google.com/workspace/guides/create-credentials#desktop-app):
   on the **Clients** page click **Create client** and select the **Desktop
   app** application type. Other client types are rejected with
   `redirect_uri_mismatch`.
1. Copy the client ID and the client secret.

```sql
CREATE PERSISTENT SECRET gd (
    TYPE gdrive,
    PROVIDER oauth,
    CLIENT_ID '<client-id>.apps.googleusercontent.com',
    CLIENT_SECRET '<client-secret>'
);
```

The first query against `gdfs://` prints the authorization URL on stderr and
waits up to five minutes for you to finish in the browser.

Notes:

- **The browser must reach `127.0.0.1` on the machine running DuckDB.** On a
  remote machine, set a fixed port with `REDIRECT_PORT 8085` and forward it
  (`ssh -L 8085:127.0.0.1:8085 host`) before opening the URL locally. Without
  `REDIRECT_PORT`, a free port is picked for each login.
- **Refresh tokens of apps in "Testing" expire after 7 days.** Google states
  that a project "with an OAuth consent screen configured for an external user
  type and a publishing status of "Testing" is issued a refresh token expiring
  in 7 days"
  ([Using OAuth 2.0](https://developers.google.com/identity/protocols/oauth2#expiration)).
  Expect to sign in again weekly unless the app is internal to your organization
  or published.
- Google shows an "unverified app" warning for apps that request Drive access
  and have not completed its verification process.
- Google's device flow ("enter this code on another device") is not used because
  it
  [does not allow the Drive scope](https://developers.google.com/identity/protocols/oauth2/limited-input-device).

### A token you already have (`PROVIDER token`)

Any access token with a Drive scope works. With the Google Cloud CLI:

```bash
gcloud auth login --enable-gdrive-access
gcloud auth print-access-token
```

See the reference for
[`gcloud auth login`](https://docs.cloud.google.com/sdk/gcloud/reference/auth/login)
and
[`gcloud auth print-access-token`](https://docs.cloud.google.com/sdk/gcloud/reference/auth/print-access-token).
Access tokens are short-lived, typically one hour.

## Dropbox

### Create an app

Follow the
[Dropbox quick start](https://docs.dropboxapi.com/dropbox-api/docs/get-started/quick-start):

1. In the [Developer Console](https://www.dropbox.com/developers/console/apps),
   click **Create app**.
1. Choose **Scoped access**, then **App folder** or **Full Dropbox**.
1. Name the app and click **Create app**.
1. On the **Permissions** tab, enable the scopes below and click **Save
   changes**.

| To do         | Scope                 |
| ------------- | --------------------- |
| List and stat | `files.metadata.read` |
| Read content  | `files.content.read`  |
| Write content | `files.content.write` |

Enable scopes before authorizing: changes do not apply to tokens that were
already issued.

The **App key** (`APP_KEY`) and **App secret** (`APP_SECRET`) are on the app's
**Settings** tab; click **Reveal app secret** to see the secret
([Developer Console guide](https://docs.dropboxapi.com/dropbox-api/docs/get-started/tutorial/app-console)).

### Interactive login (`PROVIDER oauth`)

```sql
CREATE SECRET dbx (
    TYPE dropbox,
    PROVIDER oauth,
    APP_KEY 'your-app-key',
    APP_SECRET 'your-app-secret'
);
```

On first access CloudFS prints an authorization URL. Open it, approve the app,
and paste the code Dropbox shows back into the terminal. This is the code flow
without a redirect URI, with `token_access_type=offline` so that a refresh token
is returned ([OAuth guide](https://docs.dropboxapi.com/dropbox-api/docs/oauth)).
Because the code is read from standard input, this provider needs an interactive
terminal.

### A token you already have (`PROVIDER token`)

On the app's **Settings** tab, in the **OAuth 2** section, click **Generate
access token**. The token works for your own account only and is short-lived
(Dropbox's documented example is 4 hours).

## SFTP

| `PROVIDER` | Options                             | Notes                                |
| ---------- | ----------------------------------- | ------------------------------------ |
| `keyfile`  | `KEY_PATH`, `PASSPHRASE` (optional) | Path to a private key file           |
| `agent`    | none                                | Uses the running `ssh-agent`         |
| `password` | `PASSWORD`                          | Prefer a key where the server allows |

Generate a key pair with [`ssh-keygen`](https://man.openbsd.org/ssh-keygen) (it
creates an Ed25519 key by default) and install the public key on the server. For
`agent`, start [`ssh-agent`](https://man.openbsd.org/ssh-agent) and add the key
with `ssh-add`; CloudFS finds the agent through `SSH_AUTH_SOCK`.

```sql
CREATE SECRET s (TYPE sftp, PROVIDER keyfile, KEY_PATH '/home/me/.ssh/id_ed25519');
SELECT * FROM 'sftp://me@server.example.com/data/file.parquet';
```

## VFS agent

The token is whatever you start `cloudfs-agent` with:

```bash
./cloudfs-agent --token 'choose-a-long-random-string' --port 8765 --root /data
```

```sql
CREATE SECRET v (TYPE vfs, TOKEN 'choose-a-long-random-string');
```

## Storing secrets

- **Several accounts of one type.** Give each secret a `SCOPE` (a URL prefix);
  the secret with the longest matching scope is used for a path.

  ```sql
  CREATE SECRET a (TYPE vfs, TOKEN '...', SCOPE 'vfs://host-a:8765');
  CREATE SECRET b (TYPE vfs, TOKEN '...', SCOPE 'vfs://host-b:8765');
  ```

- **Persistent secrets.** `CREATE PERSISTENT SECRET` saves the secret under
  `~/.duckdb/stored_secrets`. DuckDB stores it **unencrypted**, readable only by
  your user.

- **OAuth refresh tokens.** For a persistent `oauth` secret, CloudFS also saves
  the refresh token it receives at login, so later sessions do not ask you to
  sign in again. To keep that token out of the file, add `PERSIST_TOKEN false`;
  you then sign in once per session.

- **CLI history.** `CREATE SECRET` statements typed in the DuckDB shell are
  saved in its history file in plain text.
