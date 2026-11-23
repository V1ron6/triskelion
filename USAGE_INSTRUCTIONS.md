<p align="center"><img src="assets/logo.svg" width="96" alt="Triskelion logo"></p>

# Triskelion: usage instructions

Triskelion delivers updates to your own Android and Windows apps. Apps check the server on launch and on a timer. When you publish a release, the server also pings running apps so they check right away. Every update is signed by you offline, and the app verifies it before installing.

Three things to keep straight:

| Thing | Where it lives | Secret? |
|---|---|---|
| **Private key** (`counter.key`) | Your machine only, offline | Yes. Never commit it, never upload it |
| **Public key** | Inside each app, and registered on the server | No |
| **Admin token** (`ADMIN_TOKEN`) | Server env + your shell/admin panel | Yes |

---

## 1. Deploy the server (once)

You need: a NeonDB database, a Cloudflare R2 bucket (with an access key), and a Render account.

1. Create the R2 bucket (for example `triskelion-releases`) and an API token with read and write access to it.
2. Locally, `cd server && cp config/.env.example config/.env` and fill in:
   - `DATABASE_URL` (NeonDB connection string)
   - `ADMIN_TOKEN` (a long random string you invent)
   - `R2_ACCOUNT_ID`, `R2_ACCESS_KEY_ID`, `R2_SECRET_ACCESS_KEY`, `R2_BUCKET`
   - `CORS_ORIGIN` (the admin panel's address; `*` while testing)
3. `npm install`, then `npm run migrate` (creates the tables), then `npm start` to check it runs. Open `/health`.
4. Deploy to Render with `render.yaml` (New > Blueprint). Enter the same env vars in the dashboard.
   - Use a paid instance if you want live pings: free instances sleep and drop the connections. Apps still update on their timer either way.
5. Note your URL, for example `https://triskelion.onrender.com`.

All server config lives in `server/config/`.

## 2. Create a signing key and register each app (once per app)

```bash
node tools/sign/sign.js keygen ./counter.key
```

It prints the **public key**. Keep `counter.key` somewhere safe and offline. If you lose it you can no longer ship updates to apps that embed the old public key.

Register the app on the server (the app id is lowercase letters, digits, `.`, `_`, `-`; 2 to 64 characters):

```bash
export TRISKELION_URL=https://triskelion.onrender.com
export ADMIN_TOKEN=your-admin-token

curl -X POST $TRISKELION_URL/admin/apps \
  -H "Authorization: Bearer $ADMIN_TOKEN" -H 'content-type: application/json' \
  -d '{"id":"counter","name":"Counter","publicKey":"<public key from keygen>"}'
```

One app id covers both platforms; the platform (`android` or `windows`) is chosen when you publish.

## 3. Put the updater inside your app (once per app)

The updater is a library inside the app. Users install only your app. Keep the Triskelion repo as a git submodule (or a sibling folder) so your app can build it.

### Windows (C++ / CMake)

In your `CMakeLists.txt`:

```cmake
add_subdirectory(path/to/triskelion/sdk-windows triskelion-build)
target_link_libraries(MyApp PRIVATE triskelion_win)
```

OpenSSL is required: `vcpkg install openssl:x64-windows-static`, and configure CMake with the vcpkg toolchain file. In your app:

```cpp
#include "triskelion_win/windows_client.hpp"

triskelion_win::Config cfg;
cfg.server_url = "https://triskelion.onrender.com";
cfg.app_id = "counter";
cfg.current_version = "1.0.0";               // x.y.z of THIS build
cfg.public_key_b64 = "<public key>";
cfg.mode = triskelion_win::ApplyMode::ReplaceExe;   // release = the app's single .exe
// ApplyMode::Installer for a setup .exe/.msi (then exit the app once Applied)

triskelion_win::WindowsClient updater(cfg, [](const triskelion::RunResult& r) {
    if (r.outcome == triskelion::Outcome::Applied) { /* tell the user to restart */ }
});
updater.start();        // at launch; stop() or the destructor at exit
updater.check_now();    // for a "Check for updates" button
```

Keep the user-visible version and `current_version` in one place so they never drift.

### Android (Kotlin)

Settings:

```kotlin
// settings.gradle.kts
include(":triskelion")
project(":triskelion").projectDir = file("path/to/triskelion/sdk-android")
// app/build.gradle.kts
dependencies { implementation(project(":triskelion")) }
```

Install the NDK and CMake from Android Studio's SDK Manager. Your `versionName` must be `x.y.z`. Then, in an `Application` class (declared in the manifest, not in an Activity):

```kotlin
Triskelion.init(this, Triskelion.Config(
    serverUrl = "https://triskelion.onrender.com",
    appId = "counter",
    publicKeyB64 = "<public key>",
    useFcm = false,          // true only if you set up Firebase (section 8)
))
// "Check for updates" button: Triskelion.checkNow()
```

How installs behave on Android: the app hands the verified APK to the system installer and the user confirms (a notification opens the prompt). The user must allow "install unknown apps" for your app once. Every APK must be signed with the same key as the one installed, otherwise Android refuses it.

### C#, Python, Electron (Windows)

Build `sdk-windows` with `-DTRISKELION_BUILD_DLL=ON` to get `triskelion.dll` and use its plain C functions (`triskelion_win_c.h`) through P/Invoke or ctypes.

## 4. Publish a release

1. Bump the version in the app (it must be higher than anything already out), build it, and keep the result file (the `.apk`, or the `.exe`).
2. Publish:

```bash
export TRISKELION_URL=https://triskelion.onrender.com ADMIN_TOKEN=...

node tools/sign/sign.js publish --key ./counter.key --app counter --platform android \
  --version 1.1.0 --file ./app-release.apk --changelog "What changed"

node tools/sign/sign.js publish --key ./counter.key --app counter --platform windows \
  --version 1.1.0 --file ./CounterApp.exe
```

This signs the file locally, uploads it to R2, registers the release and pings the apps that are behind.

Options:
- `--rollout 20` offers it to about 20% of installs first (see section 5).
- `--min-version 1.0.5` marks installs below that version as mandatory.
- `--channel beta` publishes to beta testers only (apps with `channel = "beta"`).
- `sign.js sign ...` (instead of `publish`) only prints the signed release and touches no network.

Order matters when you ship both platforms: publish each platform separately; an app only ever sees releases for its own platform.

## 5. Staged rollout, yank, rollback

**Widen a rollout** (re-pings the newly included installs):

```bash
curl -X PATCH $TRISKELION_URL/admin/releases/<id> -H "Authorization: Bearer $ADMIN_TOKEN" \
  -H 'content-type: application/json' -d '{"rolloutPct":100}'
```

**Yank a bad release** (stops offering it):

```bash
curl -X POST $TRISKELION_URL/admin/releases/<id>/yank -H "Authorization: Bearer $ADMIN_TOKEN"
```

Release ids come from `GET /admin/releases?appId=counter` or the admin panel.

**Rollback does not exist as a downgrade.** Apps refuse to install a version lower than or equal to the current one. If a release is bad: yank it, fix the problem, and publish a higher version (for example 1.1.1). Installs already on the bad one get the fix as a normal update.

## 6. Admin panel

```bash
cd admin
npm install
npm run dev        # http://localhost:5173
```

Sign in with the server URL and your `ADMIN_TOKEN`. You can see apps, releases, installs per version, and the event log, change the rollout percentage, and yank releases. Publishing stays in the command line on purpose, so the private key never touches a browser.

For hosting, run `npm run build` and put `admin/dist` on any static host, then set the server's `CORS_ORIGIN` to that address.

## 7. Try it without any hosting

`tools/demo/demo.py` runs the whole flow on your machine against a local server (publish, ping, install, and a tampered file being rejected). Build the core as a shared library first; the command is at the top of the script.

## 8. Optional: Firebase push for Android

Without it, Android apps still update on their timer and through the live ping while the app is open. With it, a backgrounded app is woken as well.

1. Create a Firebase project and add your Android app. Put `google-services.json` in your app module and add the Google services Gradle plugin.
2. Create a service-account key (Project settings > Service accounts) and paste the whole JSON, on one line, into the server's `FCM_SERVICE_ACCOUNT_JSON`.
3. Set `useFcm = true` in `Triskelion.Config`.

## 9. What apps check before installing

Apps refuse the update if any of these fail, and nothing is installed:
- the signature does not verify against the public key built into the app
- the signed app id, platform, version, hash or size do not match
- the downloaded bytes do not match the hash
- the version is not higher than the current one
- the download address is not https

The server cannot change any of this. Even a hacked server or storage bucket cannot make an app install an update you did not sign.

## 10. Troubleshooting

| Symptom | Likely cause |
|---|---|
| App says up to date after you published | App version is already equal or higher; the release is for another platform or channel; or the rollout percentage excludes it. Check the admin panel |
| Result `REJECTED` | The file in storage differs from the one you signed, or the public key in the app is not the one that matches `counter.key`. Re-publish, and check the key |
| Result `CHECK_FAILED` | Wrong `serverUrl`, server asleep (Render free tier), or no network. The app retries on its own |
| Updater does not start | Bad config: app id characters, version not `x.y.z`, non-https URL. The reason is logged (`Triskelion` tag on Android) or thrown on Windows |
| Android install does nothing | "Install unknown apps" not allowed for your app, or the APK is signed with a different key than the installed one |
| Windows update applied but old version still running | The running app must be restarted; the new exe takes effect on the next launch |
| Server fails to start | A required env var is missing; the error names it. Node 22.9 or newer is needed |
| Publish fails with 401 | `ADMIN_TOKEN` in your shell differs from the server's |

## 11. Safety checklist

- Private key file: offline, backed up, never in git (`*.key` is already ignored).
- `ADMIN_TOKEN`: long and random; rotate it by changing the env var and redeploying.
- Test each new release on one device before widening the rollout (`--rollout 10`).
- Keep the version number in the app and the published version identical, and always increasing.
