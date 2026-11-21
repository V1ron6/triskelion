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
tools/sign/    keygen, sign and one-command publish
core/          C++ client core         (next)
sdk-android/   Kotlin wrapper + FCM    (next)
sdk-windows/   Windows updater         (next)
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
cp .env.example .env        # fill in DATABASE_URL, ADMIN_TOKEN, R2_*
npm install
npm run migrate             # applies schema.sql to NeonDB
npm start
npm test                    # in-memory Postgres, no external services needed
```

Deploy on Render with `render.yaml` (Blueprint). Set the env vars marked `sync: false` in the dashboard.
Use a paid instance if you want SSE connections to stay alive; free instances spin down when idle.

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
- [ ] C++ client core
- [ ] Windows updater
- [ ] Android SDK + FCM
- [ ] Admin panel
