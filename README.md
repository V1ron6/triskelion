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

See [USAGE_INSTRUCTIONS.md](USAGE_INSTRUCTIONS.md) for the step-by-step guide. It covers key rotation and signing from the admin page.

## Layout

```
server/        Express API (Render), NeonDB, Cloudflare R2
  config/      all server config: env loader, .env.example, schema.sql
tools/sign/    keygen, sign and one-command publish
core/          C++ client core: version, hash, signature verify, update decision, check->download->verify->apply flow (OpenSSL)
sdk-android/   Android library: Kotlin (HTTP, SSE, installer, FCM) over a JNI bridge to the core; not yet compiled with Gradle, see below
sdk-windows/   Windows library the app embeds: WindowsClient (WinHTTP, SSE, appliers) + triskelion.dll for C#/Python/Electron; not yet compiled on Windows
admin/         React admin panel (view apps, releases, rollout, yank, installs, events)
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

Optional push for backgrounded Android apps: set `FCM_SERVICE_ACCOUNT_JSON` (the whole Firebase service-account JSON on one line). Without it, Android apps are still pinged over SSE while open and still pull on a timer.

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

## Adding Triskelion to an app (C++ / CMake)

Keep this repo on GitHub and tag releases (`git tag v0.1.0 && git push --tags`). In the app's `CMakeLists.txt`:

```cmake
include(FetchContent)
FetchContent_Declare(triskelion
  GIT_REPOSITORY https://github.com/<you>/triskelion.git
  GIT_TAG v0.1.0                 # pin a tag; changing it is how you upgrade
  SOURCE_SUBDIR sdk-windows)
FetchContent_MakeAvailable(triskelion)
target_link_libraries(myapp PRIVATE triskelion_win)
```

Nothing is copied into the app. CMake fetches the pinned tag at configure time and builds it with the app. Tests and the example exe are skipped unless you pass `-DTRISKELION_BUILD_TESTS=ON` / `-DTRISKELION_BUILD_EXAMPLE=ON`. The app only needs OpenSSL available (vcpkg). Then write the `Config` shown below, once, in the app's own code.

Verified with a throwaway app pulling the `core` target from a git tag; the Windows target follows the same mechanism but has not been built on Windows. Apps that are not C++ cannot link this directly and need a C-interface DLL (not written yet).

## Windows: embedded in the app

There is no separate updater to install. `sdk-windows/` builds a static library, `triskelion_win`, that the app links. Users install only the app.

```cpp
#include "triskelion_win/windows_client.hpp"

triskelion_win::Config cfg;
cfg.server_url      = "https://your-service.onrender.com";
cfg.app_id          = "hollow";
cfg.current_version = "1.0.0";                 // this build's version; bump per release
cfg.public_key_b64  = "<public key from keygen>";

triskelion_win::WindowsClient updater(cfg, [](const triskelion::RunResult& r) {
  if (r.outcome == triskelion::Outcome::Applied) { /* show "Update ready, restart" */ }
});
updater.start();    // on launch; stop() on exit (the destructor stops it too)
```

While the app runs it checks on launch and every 6 h, listens for broadcasts, and pulls immediately on one. A closed app cannot hear a broadcast, so it catches up on its next launch. After the first successful apply the client goes idle, because the running build is still the old version.

`core/` holds all of that logic as `triskelion::Client`: timer, broadcast listener, retries, shutdown. Failures retry after 15 min; a rejected (tampered or bad) release waits the full interval instead of hammering the server; registration retries until the server accepts it. It is tested with fakes, on real threads.

Apply modes (`cfg.mode`):
- **Installer:** the release is an `.exe`/`.msi`, started silently (`/VERYSILENT /NORESTART` Inno Setup, `/S` NSIS, `/qn` MSI). The app should exit once `Applied`.
- **ReplaceExe:** the release is the app's single `.exe`. The running exe is renamed to `.old` and replaced; a failed copy rolls back. The new version runs on the next launch.

Per-user data (downloads, a random install id) lives in `%LOCALAPPDATA%\<app>\`. Installing the app per-user, not under Program Files, avoids UAC prompts when updating.

`example/updater.cpp` builds `triskelion-updater.exe`, a command-line host for testing a deployment (`--watch` to stay running). It is a test tool, not something to ship.

Build on Windows: `vcpkg install openssl:x64-windows-static`, then CMake with the vcpkg toolchain from `sdk-windows/`.

**Status:** the Windows files have not been compiled with a real toolchain or run (the build environment was Linux). They were syntax-checked against signature-faithful stub headers, and everything they delegate to is tested: `core_tests`, `updater_tests`, `client_tests` (including ThreadSanitizer). Untested for real: WinHTTP calls, process launch, the exe swap, and `stop()` timing while a connection is still opening (up to about 5 s).

## Admin panel

`admin/` is a React + Vite app for looking at and steering releases: apps, releases, staged rollout slider, yank, installs per version, audit events. Run `cd admin && npm install && npm run dev`, then sign in with your `ADMIN_TOKEN` (kept in sessionStorage only). Publishing stays in the offline CLI on purpose, so the signing key never sits in a browser. Build with `npm run build` and host `dist/` anywhere static (set `CORS_ORIGIN` on the server to that origin).

## Non-C++ apps (C#, Python, Electron)

Build `sdk-windows` with `-DTRISKELION_BUILD_DLL=ON` to get `triskelion.dll`, a plain C interface (`triskelion_win_c.h`). From C# declare the functions with `[DllImport("triskelion")]`; a config struct and a handful of calls (create, start, check_now, stop, destroy) is all it exposes. The underlying C ABI for other platforms is `core/include/triskelion/capi.h`.

## Android

`sdk-android/` is an Android library module. The Kotlin layer does HTTP, the SSE stream, the PackageInstaller hand-off and FCM; everything security-relevant (signature, hash, size, version, no downgrade) runs in the same C++ core as Windows, reached through `jni_bridge.cpp`.

Add to the app (module is inside the Triskelion repo so its CMake can find `core/`; use a git submodule):

```kotlin
// settings.gradle.kts
include(":triskelion")
project(":triskelion").projectDir = File("triskelion/sdk-android")
// app/build.gradle.kts
dependencies { implementation(project(":triskelion")) }
```

```kotlin
class MyApp : Application() {
    override fun onCreate() {
        super.onCreate()
        Triskelion.init(this, Triskelion.Config(
            serverUrl = "https://your-service.onrender.com",
            appId = "locked",
            publicKeyB64 = "<public key from keygen>",
        ))
    }
}
```

Notes:
- Your `versionName` must be `x.y.z`, matching what you publish.
- FCM needs `google-services.json` and the Google services Gradle plugin in the app. Pass `useFcm = false` to skip it.
- Android always asks the user to confirm an install (a notification opens the system prompt), unless the OS permits a silent same-installer update. The user must allow "install unknown apps" for your app once.
- Sign every APK with the same key as the installed one, or Android refuses the update.
- The JNI bridge is tested on a desktop JVM (`sdk-android/jni-test/run.sh`: full flow, bad config, throwing host, unicode paths, stale pings, lifecycle; also clean under ASan/UBSan). The Kotlin and Gradle parts have not been compiled yet.

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
- [x] Windows library written (needs a first compile and a real end-to-end run on Windows)
- [x] Android SDK written: JNI bridge tested on a JVM; Kotlin/Gradle need a first build
- [x] Server FCM notifier (tested with a fake FCM)
- [x] C interface + `triskelion.dll` for non-C++ apps (needs a first Windows build)
- [x] Admin panel
