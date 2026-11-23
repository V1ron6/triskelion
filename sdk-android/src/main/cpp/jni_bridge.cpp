// JNI glue between com.triskelion.sdk.NativeBridge and the C interface (capi.h).
// The core calls back from its own threads, so every callback attaches the thread to the JVM
// (detached again when the thread ends) and runs inside a local-reference frame, because references
// created on an attached native thread are otherwise never released.
#include <jni.h>

#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "triskelion/capi.h"

namespace {

constexpr const char* kBridge = "com/triskelion/sdk/NativeBridge";
constexpr const char* kHost = "com/triskelion/sdk/NativeBridge$Host";
constexpr const char* kReply = "com/triskelion/sdk/NativeBridge$HttpReply";

JavaVM* g_vm = nullptr;
jfieldID g_reply_status = nullptr;
jfieldID g_reply_body = nullptr;
jmethodID m_get, m_post, m_download, m_stream_run, m_stream_abort, m_apply, m_on_result;

// ---- thread attachment --------------------------------------------------------------------------

struct Detacher {
  JavaVM* vm = nullptr;
  ~Detacher() { if (vm) vm->DetachCurrentThread(); }
};
thread_local Detacher t_detacher;

JNIEnv* env_for_thread() {
  JNIEnv* env = nullptr;
  if (g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) return env;
#ifdef __ANDROID__
  if (g_vm->AttachCurrentThreadAsDaemon(&env, nullptr) != JNI_OK) return nullptr;
#else
  if (g_vm->AttachCurrentThreadAsDaemon(reinterpret_cast<void**>(&env), nullptr) != JNI_OK) return nullptr;
#endif
  t_detacher.vm = g_vm;
  return env;
}

struct Frame {
  JNIEnv* env;
  bool ok;
  Frame(JNIEnv* e, int capacity) : env(e), ok(e->PushLocalFrame(capacity) == 0) {}
  ~Frame() { if (ok) env->PopLocalFrame(nullptr); }
};

// A Java exception from a host callback must never unwind into the core: clear it and report failure.
bool threw(JNIEnv* env) {
  if (!env->ExceptionCheck()) return false;
  env->ExceptionClear();
  return true;
}

// ---- UTF-8 <-> Java strings (JNI's own helpers use "modified UTF-8", which breaks emoji and NUL) -

void append_utf8(std::string& out, unsigned cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

std::string to_utf8(JNIEnv* env, jstring s) {
  std::string out;
  if (!s) return out;
  const jsize n = env->GetStringLength(s);
  const jchar* c = env->GetStringChars(s, nullptr);
  if (!c) return out;
  for (jsize i = 0; i < n; i++) {
    unsigned cp = c[i];
    if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < n && c[i + 1] >= 0xDC00 && c[i + 1] < 0xE000) {
      cp = 0x10000 + ((cp - 0xD800) << 10) + (c[i + 1] - 0xDC00);
      i++;
    } else if (cp >= 0xD800 && cp < 0xE000) {
      cp = 0xFFFD;  // lone surrogate
    }
    append_utf8(out, cp);
  }
  env->ReleaseStringChars(s, c);
  return out;
}

jstring from_utf8(JNIEnv* env, const char* s) {
  std::vector<jchar> u16;
  const unsigned char* p = reinterpret_cast<const unsigned char*>(s ? s : "");
  while (*p) {
    unsigned cp = 0xFFFD;
    int extra = 0;
    if (*p < 0x80) { cp = *p; }
    else if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1F; extra = 1; }
    else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0F; extra = 2; }
    else if ((*p & 0xF8) == 0xF0) { cp = *p & 0x07; extra = 3; }
    p++;
    bool valid = true;
    for (int i = 0; i < extra; i++) {
      if ((*p & 0xC0) != 0x80) { valid = false; break; }
      cp = (cp << 6) | (*p & 0x3F);
      p++;
    }
    if (!valid || cp > 0x10FFFF || (cp >= 0xD800 && cp < 0xE000)) cp = 0xFFFD;
    if (cp >= 0x10000) {
      cp -= 0x10000;
      u16.push_back(static_cast<jchar>(0xD800 + (cp >> 10)));
      u16.push_back(static_cast<jchar>(0xDC00 + (cp & 0x3FF)));
    } else {
      u16.push_back(static_cast<jchar>(cp));
    }
  }
  return env->NewString(u16.data(), static_cast<jsize>(u16.size()));
}

jbyteArray to_bytes(JNIEnv* env, const char* data, size_t n) {
  jbyteArray a = env->NewByteArray(static_cast<jsize>(n));
  if (a && n) env->SetByteArrayRegion(a, 0, static_cast<jsize>(n), reinterpret_cast<const jbyte*>(data));
  return a;
}

// ---- host callbacks (the core calls these) ---------------------------------------------------------

struct JniHost {
  jobject host;  // global reference, released in destroy after the core's threads have ended
};
jobject host_of(void* user) { return static_cast<JniHost*>(user)->host; }

void fill_response(JNIEnv* env, jobject reply, tri_response* out) {
  if (!reply) return;  // null = transport failure
  const jint status = env->GetIntField(reply, g_reply_status);
  jbyteArray arr = static_cast<jbyteArray>(env->GetObjectField(reply, g_reply_body));
  std::string body;
  if (arr) {
    const jsize n = env->GetArrayLength(arr);
    body.resize(static_cast<size_t>(n));
    if (n) env->GetByteArrayRegion(arr, 0, n, reinterpret_cast<jbyte*>(&body[0]));
  }
  tri_response_set(out, status, body.data(), body.size());
}

void c_http_get(void* user, const char* url, tri_response* out) {
  JNIEnv* env = env_for_thread();
  if (!env) return;
  Frame f(env, 16);
  jobject reply = env->CallObjectMethod(host_of(user), m_get, from_utf8(env, url));
  if (threw(env)) return;
  fill_response(env, reply, out);
}

void c_http_post(void* user, const char* url, const char* body, tri_response* out) {
  JNIEnv* env = env_for_thread();
  if (!env) return;
  Frame f(env, 16);
  const size_t len = std::char_traits<char>::length(body);
  jobject reply = env->CallObjectMethod(host_of(user), m_post, from_utf8(env, url), to_bytes(env, body, len));
  if (threw(env)) return;
  fill_response(env, reply, out);
}

int c_download(void* user, const char* url, const char* dest) {
  JNIEnv* env = env_for_thread();
  if (!env) return 0;
  Frame f(env, 16);
  const jboolean ok = env->CallBooleanMethod(host_of(user), m_download, from_utf8(env, url), from_utf8(env, dest));
  return !threw(env) && ok ? 1 : 0;
}

// Pings come back from Java as an opaque handle. A registry makes a stale or bogus handle harmless.
struct PingCtx {
  tri_ping_fn fn;
  void* ctx;
};
std::mutex g_ping_mutex;
std::unordered_set<jlong> g_pings;

void c_stream_run(void* user, const char* url, tri_ping_fn fn, void* ctx) {
  JNIEnv* env = env_for_thread();
  if (!env) return;
  Frame f(env, 16);
  PingCtx* p = new PingCtx{fn, ctx};
  const jlong handle = reinterpret_cast<jlong>(p);
  { std::lock_guard<std::mutex> l(g_ping_mutex); g_pings.insert(handle); }
  env->CallVoidMethod(host_of(user), m_stream_run, from_utf8(env, url), handle);
  threw(env);
  { std::lock_guard<std::mutex> l(g_ping_mutex); g_pings.erase(handle); }  // waits out any ping still running
  delete p;
}

void c_stream_abort(void* user) {
  JNIEnv* env = env_for_thread();
  if (!env) return;
  Frame f(env, 8);
  env->CallVoidMethod(host_of(user), m_stream_abort);
  threw(env);
}

int c_apply(void* user, const char* path) {
  JNIEnv* env = env_for_thread();
  if (!env) return 0;
  Frame f(env, 8);
  const jboolean ok = env->CallBooleanMethod(host_of(user), m_apply, from_utf8(env, path));
  return !threw(env) && ok ? 1 : 0;
}

void c_on_result(void* user, int outcome, const char* detail, const char* version) {
  JNIEnv* env = env_for_thread();
  if (!env) return;
  Frame f(env, 8);
  env->CallVoidMethod(host_of(user), m_on_result, static_cast<jint>(outcome), from_utf8(env, detail), from_utf8(env, version));
  threw(env);
}

// ---- native methods -------------------------------------------------------------------------------

struct NativeClient {
  tri_client* client = nullptr;
  JniHost host{nullptr};
};
NativeClient* client_of(jlong h) { return reinterpret_cast<NativeClient*>(h); }

void set_error(JNIEnv* env, jobjectArray errOut, const char* msg) {
  if (errOut && env->GetArrayLength(errOut) > 0) env->SetObjectArrayElement(errOut, 0, from_utf8(env, msg));
}

jlong n_create(JNIEnv* env, jclass, jobject host, jstring serverUrl, jstring appId, jstring platform, jstring version,
               jstring key, jstring channel, jstring installId, jstring workDir, jint checkMs, jint retryMs,
               jint reconnectMs, jboolean insecure, jobjectArray errOut) {
  if (!host) { set_error(env, errOut, "host is required"); return 0; }

  const std::string s_server = to_utf8(env, serverUrl), s_app = to_utf8(env, appId), s_platform = to_utf8(env, platform),
                    s_version = to_utf8(env, version), s_key = to_utf8(env, key), s_install = to_utf8(env, installId),
                    s_work = to_utf8(env, workDir);
  const std::string s_channel = channel ? to_utf8(env, channel) : std::string("stable");

  auto nc = new NativeClient();
  nc->host.host = env->NewGlobalRef(host);

  tri_host h{};
  h.user = &nc->host;
  h.http_get = c_http_get;
  h.http_post_json = c_http_post;
  h.download = c_download;
  h.stream_run = c_stream_run;
  h.stream_abort = c_stream_abort;
  h.apply = c_apply;
  h.on_result = c_on_result;

  tri_config cfg{};
  cfg.server_url = s_server.c_str();
  cfg.app_id = s_app.c_str();
  cfg.platform = s_platform.c_str();
  cfg.current_version = s_version.c_str();
  cfg.public_key_b64 = s_key.c_str();
  cfg.channel = s_channel.c_str();
  cfg.install_id = s_install.c_str();
  cfg.work_dir = s_work.c_str();
  cfg.check_interval_ms = checkMs > 0 ? static_cast<unsigned>(checkMs) : 0;
  cfg.retry_interval_ms = retryMs > 0 ? static_cast<unsigned>(retryMs) : 0;
  cfg.reconnect_delay_ms = reconnectMs > 0 ? static_cast<unsigned>(reconnectMs) : 0;
  cfg.allow_insecure_http = insecure ? 1 : 0;

  char err[160] = "";
  nc->client = tri_client_create(&cfg, &h, err, sizeof err);
  if (!nc->client) {
    set_error(env, errOut, err);
    env->DeleteGlobalRef(nc->host.host);
    delete nc;
    return 0;
  }
  return reinterpret_cast<jlong>(nc);
}

void n_start(JNIEnv*, jclass, jlong h) { if (h) tri_client_start(client_of(h)->client); }
void n_check_now(JNIEnv*, jclass, jlong h) { if (h) tri_client_check_now(client_of(h)->client); }
jboolean n_update_applied(JNIEnv*, jclass, jlong h) { return h && tri_client_update_applied(client_of(h)->client) ? JNI_TRUE : JNI_FALSE; }
void n_stop(JNIEnv*, jclass, jlong h) { if (h) tri_client_stop(client_of(h)->client); }

void n_destroy(JNIEnv* env, jclass, jlong h) {
  if (!h) return;
  NativeClient* nc = client_of(h);
  tri_client_destroy(nc->client);  // stops and joins: no callback can run after this line
  env->DeleteGlobalRef(nc->host.host);
  delete nc;
}

void n_ping(JNIEnv*, jclass, jlong handle) {
  std::lock_guard<std::mutex> l(g_ping_mutex);
  if (!g_pings.count(handle)) return;
  PingCtx* p = reinterpret_cast<PingCtx*>(handle);
  p->fn(p->ctx);
}

}  // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
  g_vm = vm;
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;

  jclass host = env->FindClass(kHost);
  jclass reply = env->FindClass(kReply);
  jclass bridge = env->FindClass(kBridge);
  if (!host || !reply || !bridge) return JNI_ERR;

  g_reply_status = env->GetFieldID(reply, "status", "I");
  g_reply_body = env->GetFieldID(reply, "body", "[B");
  const char* reply_sig = "Lcom/triskelion/sdk/NativeBridge$HttpReply;";
  m_get = env->GetMethodID(host, "get", (std::string("(Ljava/lang/String;)") + reply_sig).c_str());
  m_post = env->GetMethodID(host, "postJson", (std::string("(Ljava/lang/String;[B)") + reply_sig).c_str());
  m_download = env->GetMethodID(host, "download", "(Ljava/lang/String;Ljava/lang/String;)Z");
  m_stream_run = env->GetMethodID(host, "streamRun", "(Ljava/lang/String;J)V");
  m_stream_abort = env->GetMethodID(host, "streamAbort", "()V");
  m_apply = env->GetMethodID(host, "apply", "(Ljava/lang/String;)Z");
  m_on_result = env->GetMethodID(host, "onResult", "(ILjava/lang/String;Ljava/lang/String;)V");
  if (!g_reply_status || !g_reply_body || !m_get || !m_post || !m_download || !m_stream_run || !m_stream_abort ||
      !m_apply || !m_on_result)
    return JNI_ERR;

  const JNINativeMethod methods[] = {
      {const_cast<char*>("create"),
       const_cast<char*>("(Lcom/triskelion/sdk/NativeBridge$Host;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
                         "Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
                         "IIIZ[Ljava/lang/String;)J"),
       reinterpret_cast<void*>(n_create)},
      {const_cast<char*>("start"), const_cast<char*>("(J)V"), reinterpret_cast<void*>(n_start)},
      {const_cast<char*>("checkNow"), const_cast<char*>("(J)V"), reinterpret_cast<void*>(n_check_now)},
      {const_cast<char*>("updateApplied"), const_cast<char*>("(J)Z"), reinterpret_cast<void*>(n_update_applied)},
      {const_cast<char*>("stop"), const_cast<char*>("(J)V"), reinterpret_cast<void*>(n_stop)},
      {const_cast<char*>("destroy"), const_cast<char*>("(J)V"), reinterpret_cast<void*>(n_destroy)},
      {const_cast<char*>("ping"), const_cast<char*>("(J)V"), reinterpret_cast<void*>(n_ping)},
  };
  if (env->RegisterNatives(bridge, methods, sizeof(methods) / sizeof(methods[0])) != JNI_OK) return JNI_ERR;
  return JNI_VERSION_1_6;
}
