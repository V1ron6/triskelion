<p align="center">
  <img src="assets/logo.svg" width="160" alt="Triskelion logo">
</p>

<h1 align="center">Triskelion</h1>

<p align="center">Update delivery for my own apps (Android + Windows). Signed releases, pull-based checks, and a broadcast that tells apps to pull.</p>

---

## How it works

```
publish ──► sign (offline key) ──► artifact to R2 ──► release row in NeonDB ──► broadcast "new version X"
                                                                                      │
app ◄── ping (SSE / FCM) ◄────────────────────────────────────────────────────────────┘
 └─► GET /v1/check ─► presigned R2 URL ─► download ─► verify hash + signature ─► install
```

- **Pull always works.** Apps check on launch and on a timer. The broadcast only tells an out-of-date app to run that same check sooner.
- **The ping carries no update.** It is just `{app, version}`. A forged ping can at worst trigger a check.
- **Releases are signed offline** with Ed25519. The server verifies the signature on publish, and the client verifies it again on download, so a compromised server or bucket cannot push code to the apps.

## Layout

```
server/        Express API (Render), NeonDB, Cloudflare R2
  config/      all server config: env loader, .env.example, schema.sql
tools/sign/    keygen, sign and one-command publish
core/          C++ client core: version, hash, signature verify, update decision, check->download->verify->apply flow (OpenSSL)
sdk-android/   Kotlin wrapper + FCM    (next)
sdk-windows/   Windows updater: WinHTTP client, appliers, install id, triskelion-updater.exe (not yet compiled, see below)
admin/         React admin panel       (next)
```

## API

Public (`/v1`)

| Method | Path | Purpose |
|---|---|---|
| POST | `/register` | install registers/refreshes itself and its push token |
| GET | `/check?app&platform&version&install[&channel]` | `{update:false}` or the signed manifest + presigned download URL |
| GET | `/stream?install` | SSE channel for broadcast pings |

Admin (`/admin`, `Authorization: Bearer $ADMIN_TOKEN`)

| Method | Path | Purpose |
|---|---|---|
| POST | `/apps` | register an app and its public key |
| GET | `/apps` | list apps |
| POST | `/upload-url` | presigned PUT for the artifact |
| POST | `/releases` | register a signed release, then broadcast |
| GET | `/releases?appId=` | list releases |
| PATCH | `/releases/:id` | widen the rollout `{rolloutPct}`, re-broadcasts |
| POST | `/releases/:id/yank` | stop offering a release |

`check` fields: `update, version, sha256, size, signature, changelog, mandatory, url`.
`mandatory` is true when the install is below the release's `minVersion`.

## Setup

```bash
cd server
cp config/.env.example config/.env   # fill in DATABASE_URL, ADMIN_TOKEN, R2_*
npm install
npm run migrate             # applies config/schema.sql to NeonDB
npm start
npm test                    # in-memory Postgres, no external services needed
```

Deploy on Render with `render.yaml` (Blueprint). Set the env vars marked `sync: false` in the dashboard.
Use a paid instance if you want SSE connections to stay alive; free instances spin down when idle.

## Client core

`core/` is the shared C++17 library both platform updaters link. It does no networking; it answers one question: *is this downloaded file safe to install?*

```cpp
triskelion::Policy policy{"locked", "android", "1.0.0", PUBLIC_KEY_B64};  // baked in at build time
auto d = triskelion::decide(policy, manifest, downloaded_file);            // Decision::Install or a Reject*
```

`decide` checks, in order: versions parse, app and platform match the build, the candidate is strictly newer, the Ed25519 signature verifies, then file size and SHA-256. Needs OpenSSL >= 1.1.1 (Windows: `vcpkg install openssl`; Android: prefab `openssl`).

```bash
cmake -S core -B core/build && cmake --build core/build && ./core/build/core_tests
node core/test/gen_fixture.mjs   # only to regenerate the fixture
```

The test fixture is produced by `tools/sign`, so the C++ verifier is checked against the exact bytes the real tool signs.

`run_update(config, http, applier)` is the whole flow: check, refuse anything not newer or not https, download, `decide`, then hand the verified file to an `Applier`. Platforms only implement `Http` (sockets) and `Applier` (how to install). Failed or rejected downloads are deleted, and the file name uses the validated `x.y.z`, never raw server text.

## Windows updater

`sdk-windows/` builds `triskelion-updater.exe`. Per app, edit `src/app_config.hpp` (server URL, app id, current version, **public** key, apply mode) and rebuild.

```
triskelion-updater.exe            check once, apply if verified, exit
triskelion-updater.exe --watch    check now, every 6 h, and instantly on a broadcast
```

Two apply modes, chosen at build time with `TRISKELION_APPLY_MODE_INSTALLER`:
- **Installer (1):** the release is an `.exe`/`.msi`, started silently (`/VERYSILENT /NORESTART` for Inno Setup, `/S` NSIS, `/qn` MSI). The app should exit once `apply` succeeds.
- **Replace (0):** the release is the app's single `.exe`. The running exe is renamed to `.old`, the new one is copied in, and a failed copy rolls back. New version runs on next launch.

Downloads and the random install id live in `%LOCALAPPDATA%\<app>\`, per user. Build on Windows: `vcpkg install openssl:x64-windows-static`, then CMake with the vcpkg toolchain, from `sdk-windows/`.

**Status: the Windows code has not been compiled or run yet** (the build environment was Linux). The shared logic it calls is tested; the WinHTTP, process-launch and file-swap code is not.

## Publishing a release

```bash
# once per app: keep the key file offline, never commit it
node tools/sign/sign.js keygen ./locked.key
curl -X POST $TRISKELION_URL/admin/apps -H "Authorization: Bearer $ADMIN_TOKEN" \
  -H 'content-type: application/json' \
  -d '{"id":"locked","name":"Locked","publicKey":"<public key from keygen>"}'

# per release
export TRISKELION_URL=https://your-service.onrender.com ADMIN_TOKEN=...
node tools/sign/sign.js publish --key ./locked.key --app locked --platform android \
  --version 1.1.0 --file ./locked.apk --rollout 20 --changelog "Pomodoro fixes"
```

`sign` (instead of `publish`) prints the release JSON without touching the network.

## Rules the clients must follow

1. Embed the app's public key at build time. Never fetch it from the server.
2. Rebuild the signed message locally from `app`, `platform`, `version`, `sha256`, `size` and verify the signature before installing.
3. Hash the downloaded bytes and compare to `sha256`.
4. Never install a version lower than or equal to the current one.

Signed message format: `triskelion:v1\n<app>\n<platform>\n<version>\n<sha256>\n<size>`.

## Notes

- **Rollback = yank + roll forward.** Yanking stops a bad release being offered, but clients refuse downgrades, so installs already on it need a higher fixed version.
- Staged rollout uses a stable per-install bucket (0-99), so an install never flips in and out as the percentage changes.
- Rate limiting is in-memory per instance. Fine for one instance, revisit before running several.
- `owner` on `apps` is the hook for opening this up to other users later.

## Status

- [x] Server: register, check, stream, publish, staged rollout, yank, audit events
- [x] Signing tool
- [x] C++ client core: verification and update decision (network and install steps come with the platform updaters)
- [x] Windows updater written (needs a first compile and a real end-to-end run on Windows)
- [ ] Android SDK + FCM
- [ ] Admin panel
