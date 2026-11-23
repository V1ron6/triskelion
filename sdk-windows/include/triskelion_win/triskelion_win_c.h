/* triskelion.dll: C interface to the Windows updater, for apps that are not C++ (C#, Python, Electron,
 * Delphi, Rust...). Same behavior as triskelion_win::WindowsClient. All strings are UTF-8 except the
 * two path/argument fields, which are UTF-16 (wchar_t) to match Windows APIs.
 *
 * Outcome codes passed to the callback:
 *   0 up to date | 1 applied (installer started / exe replaced) | 2 check failed | 3 download failed
 *   4 rejected (failed verification) | 5 apply failed
 * The callback runs on a background thread. Do not call stop/destroy from inside it.
 *
 * C# (P/Invoke) sketch:
 *   [DllImport("triskelion.dll")] static extern IntPtr triskelion_win_create(ref Config c, Callback cb, IntPtr user, StringBuilder err, UIntPtr errLen);
 *   [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate void Callback(IntPtr user, int outcome, IntPtr detail, IntPtr version);
 * Keep the Callback delegate in a field so the garbage collector does not free it while native code holds it.
 */
#ifndef TRISKELION_WIN_C_H
#define TRISKELION_WIN_C_H

#include <stddef.h>
#include <wchar.h>

#ifdef _WIN32
#ifdef TRISKELION_WIN_BUILD_DLL
#define TRI_WIN_API __declspec(dllexport)
#else
#define TRI_WIN_API __declspec(dllimport)
#endif
#else
#define TRI_WIN_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct triskelion_win_client triskelion_win_client;

typedef void (*triskelion_win_callback)(void* user, int outcome, const char* detail, const char* version);

typedef struct triskelion_win_config {
  const char* server_url;       /* https://... */
  const char* app_id;           /* [a-z0-9._-], must match the app registered on the server */
  const char* current_version;  /* x.y.z of THIS build */
  const char* public_key_b64;   /* PUBLIC Ed25519 key, never the private one */
  const char* channel;          /* "stable" or "beta"; NULL = stable */
  int mode;                     /* 0 = run a silent installer, 1 = replace the app's single .exe */
  int installer_is_msi;         /* mode 0: 1 = .msi (msiexec), 0 = .exe */
  const wchar_t* installer_args;/* mode 0: NULL = L"/VERYSILENT /NORESTART". NSIS: L"/S". MSI: L"/qn" */
  const wchar_t* installed_exe; /* mode 1: NULL = the running executable */
  unsigned check_interval_minutes; /* 0 = 360 */
  int allow_insecure_http;      /* development only */
} triskelion_win_config;

/* NULL on failure; err (if non-NULL) receives a short reason. */
TRI_WIN_API triskelion_win_client* triskelion_win_create(const triskelion_win_config* config,
                                                         triskelion_win_callback callback, void* user,
                                                         char* err, size_t err_len);
TRI_WIN_API void triskelion_win_start(triskelion_win_client* c);
TRI_WIN_API void triskelion_win_check_now(triskelion_win_client* c);
TRI_WIN_API int triskelion_win_update_applied(const triskelion_win_client* c);
TRI_WIN_API void triskelion_win_stop(triskelion_win_client* c);
TRI_WIN_API void triskelion_win_destroy(triskelion_win_client* c); /* stops first; NULL allowed */

#ifdef __cplusplus
}
#endif
#endif /* TRISKELION_WIN_C_H */
