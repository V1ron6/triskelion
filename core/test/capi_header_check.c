/* Compiled as C (not C++) to prove capi.h is valid C and usable from C-only hosts. */
#include "triskelion/capi.h"

static void get_cb(void* user, const char* url, tri_response* out) { (void)user; (void)url; tri_response_set(out, 200, "{}", 2); }
static void post_cb(void* user, const char* url, const char* body, tri_response* out) { (void)user; (void)url; (void)body; (void)out; }
static int dl_cb(void* user, const char* url, const char* dest) { (void)user; (void)url; (void)dest; return 0; }
static void run_cb(void* user, const char* url, tri_ping_fn on_ping, void* ctx) { (void)user; (void)url; (void)on_ping; (void)ctx; }
static void abort_cb(void* user) { (void)user; }
static int apply_cb(void* user, const char* path) { (void)user; (void)path; return 0; }

int tri_c_header_smoke(void) {
  tri_host host = {0};
  host.http_get = get_cb;
  host.http_post_json = post_cb;
  host.download = dl_cb;
  host.stream_run = run_cb;
  host.stream_abort = abort_cb;
  host.apply = apply_cb;
  tri_config cfg = {0};
  char err[64];
  return tri_client_create(&cfg, &host, err, sizeof err) == 0;
}
