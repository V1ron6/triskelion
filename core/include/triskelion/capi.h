/* Triskelion C interface.
 *
 * The same tested update logic as the C++ core, behind a plain C ABI so any language can drive it
 * (Android through JNI, C#, Python, Rust...). The HOST supplies the platform pieces as callbacks:
 * HTTP, the broadcast stream, and how to install a verified file. The core does the rest: register,
 * check, download, verify (version, target, Ed25519 signature, size, SHA-256), retry, timers.
 *
 * Threading: the core calls host callbacks from its own threads. http_*, download, apply and
 * on_result come from one worker thread; stream_run comes from a second thread; stream_abort may be
 * called from any thread (including inside tri_client_stop). Callbacks must not call back into
 * tri_client_stop/destroy. All strings are UTF-8. The core copies everything it needs, so the host
 * may free its config strings once tri_client_create returns.
 */
#ifndef TRISKELION_CAPI_H
#define TRISKELION_CAPI_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tri_client tri_client;
typedef struct tri_response tri_response; /* opaque, owned by the core */

/* Host fills this from inside http_get / http_post_json. Never calling it means transport failure. */
void tri_response_set(tri_response* out, int status, const char* body, size_t body_len);

/* Call from stream_run for every broadcast received. The ping content is never trusted or parsed. */
typedef void (*tri_ping_fn)(void* ping_ctx);

typedef enum {
  TRI_UP_TO_DATE = 0,
  TRI_APPLIED = 1,
  TRI_CHECK_FAILED = 2,
  TRI_DOWNLOAD_FAILED = 3,
  TRI_REJECTED = 4, /* downloaded but failed verification: wrong signature, hash, size, target or version */
  TRI_APPLY_FAILED = 5
} tri_outcome;

typedef struct tri_host {
  void* user; /* passed back as the first argument of every callback */

  /* Small JSON requests. Fill `out` with tri_response_set. */
  void (*http_get)(void* user, const char* url, tri_response* out);
  void (*http_post_json)(void* user, const char* url, const char* body, tri_response* out);

  /* Stream the URL to dest_path (a full 200 response only). Return 1 on success, 0 otherwise. */
  int (*download)(void* user, const char* url, const char* dest_path);

  /* Block, calling on_ping(ping_ctx) per broadcast, until the connection ends or stream_abort. */
  void (*stream_run)(void* user, const char* url, tri_ping_fn on_ping, void* ping_ctx);
  /* STICKY: the current stream_run returns promptly and later ones return at once. */
  void (*stream_abort)(void* user);

  /* Called only with a file that passed verification. Return 1 if the update was put in place. */
  int (*apply)(void* user, const char* verified_file_path);

  /* Optional (may be NULL). `detail` and `version` may be empty strings, never NULL. */
  void (*on_result)(void* user, int outcome, const char* detail, const char* version);
} tri_host;

typedef struct tri_config {
  const char* server_url;      /* https://... */
  const char* app_id;          /* must match the app registered on the server */
  const char* platform;        /* "android" or "windows" */
  const char* current_version; /* x.y.z of THIS build */
  const char* public_key_b64;  /* PUBLIC Ed25519 key (32 bytes, base64), baked into the build */
  const char* channel;         /* "stable" or "beta"; NULL = "stable" */
  const char* install_id;      /* stable random id for this install: 16-64 chars of [0-9a-fA-F-] */
  const char* work_dir;        /* downloads land here; must be private to the app */
  unsigned check_interval_ms;   /* 0 = 6 hours */
  unsigned retry_interval_ms;   /* 0 = 15 minutes */
  unsigned reconnect_delay_ms;  /* 0 = 30 seconds */
  int allow_insecure_http;      /* development only: allow an http:// server_url */
} tri_config;

/* Returns NULL if the config is invalid; if err is non-NULL it receives a short reason. */
tri_client* tri_client_create(const tri_config* config, const tri_host* host, char* err, size_t err_len);

void tri_client_start(tri_client* c);      /* idempotent; first check runs immediately */
void tri_client_check_now(tri_client* c);
int tri_client_update_applied(const tri_client* c);
void tri_client_stop(tri_client* c);       /* idempotent; returns after both threads end */
void tri_client_destroy(tri_client* c);    /* stops first; c is invalid afterwards. NULL is allowed. */

#ifdef __cplusplus
}
#endif
#endif /* TRISKELION_CAPI_H */
