#!/usr/bin/env python3
"""End-to-end demo of Triskelion on one machine.

Runs the real server, publishes signed releases with the real signing tool, and runs two "counter apps"
(one Android, one Windows) through the real C++ core via its C interface. Only the platform pieces
(HTTP, stream, install) are stand-ins written here; on a device those are the Kotlin / WinHTTP hosts.

  python3 tools/demo/demo.py /path/to/libtriskelion.so
Build the lib:  g++ -shared -fPIC -std=c++17 -Icore/include core/src/*.cpp -lcrypto -lpthread -o libtriskelion.so
"""
import ctypes, http.client, json, os, shutil, ssl, subprocess, sys, tempfile, threading, time, urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LIB = ctypes.CDLL(sys.argv[1])
API, BLOB, TOKEN = 8787, 8788, "demo-admin-token"
BASE = f"http://127.0.0.1:{API}"
CTX = ssl.create_default_context(); CTX.check_hostname = False; CTX.verify_mode = ssl.CERT_NONE  # demo's self-signed blob store

OUTCOMES = ["UP_TO_DATE", "APPLIED", "CHECK_FAILED", "DOWNLOAD_FAILED", "REJECTED", "APPLY_FAILED"]

# ---- C interface (capi.h) --------------------------------------------------------------------------
VP, CP = ctypes.c_void_p, ctypes.c_char_p
PING = ctypes.CFUNCTYPE(None, VP)
T_GET = ctypes.CFUNCTYPE(None, VP, CP, VP)
T_POST = ctypes.CFUNCTYPE(None, VP, CP, CP, VP)
T_DOWNLOAD = ctypes.CFUNCTYPE(ctypes.c_int, VP, CP, CP)
T_STREAM = ctypes.CFUNCTYPE(None, VP, CP, PING, VP)
T_ABORT = ctypes.CFUNCTYPE(None, VP)
T_APPLY = ctypes.CFUNCTYPE(ctypes.c_int, VP, CP)
T_RESULT = ctypes.CFUNCTYPE(None, VP, ctypes.c_int, CP, CP)
class Host(ctypes.Structure):
    _fields_ = [("user", VP), ("http_get", T_GET), ("http_post_json", T_POST), ("download", T_DOWNLOAD),
                ("stream_run", T_STREAM), ("stream_abort", T_ABORT), ("apply", T_APPLY), ("on_result", T_RESULT)]
class Config(ctypes.Structure):
    _fields_ = [(n, CP) for n in ("server_url", "app_id", "platform", "current_version", "public_key_b64", "channel", "install_id", "work_dir")] + \
               [("check_interval_ms", ctypes.c_uint), ("retry_interval_ms", ctypes.c_uint), ("reconnect_delay_ms", ctypes.c_uint), ("allow_insecure_http", ctypes.c_int)]
LIB.tri_client_create.restype = VP
LIB.tri_client_create.argtypes = [ctypes.POINTER(Config), ctypes.POINTER(Host), ctypes.c_char_p, ctypes.c_size_t]
LIB.tri_response_set.argtypes = [VP, ctypes.c_int, CP, ctypes.c_size_t]
for f in ("start", "stop", "destroy", "check_now"): getattr(LIB, f"tri_client_{f}").argtypes = [VP]

def say(s=""): print(s, flush=True)

class CounterApp:
    """One installed copy of the counter app on one platform."""
    def __init__(self, name, platform, version, install_id, pubkey, root):
        self.name, self.platform, self.version = name, platform, version
        self.installed = root / name; self.installed.mkdir(parents=True)
        self.events, self.cv = [], threading.Condition()
        self.aborted, self.stream = False, None
        self.keep = []
        h = Host()
        h.http_get = T_GET(self._get); h.http_post_json = T_POST(self._post)
        h.download = T_DOWNLOAD(self._download); h.stream_run = T_STREAM(self._stream_run)
        h.stream_abort = T_ABORT(self._abort); h.apply = T_APPLY(self._apply)
        h.on_result = T_RESULT(self._result)
        self.keep.append(h)
        c = Config(BASE.encode(), b"counter", platform.encode(), version.encode(), pubkey.encode(), b"stable",
                   install_id.encode(), str(root / (name + "-work")).encode(), 60000, 1000, 500, 1)
        (root / (name + "-work")).mkdir()
        err = ctypes.create_string_buffer(200)
        self.c = LIB.tri_client_create(ctypes.byref(c), ctypes.byref(h), err, 200)
        assert self.c, err.value

    def _reply(self, out, status, body): LIB.tri_response_set(out, status, body, len(body))
    def _get(self, _u, url, out):
        try:
            with urllib.request.urlopen(url.decode(), timeout=10) as r: self._reply(out, r.status, r.read())
        except urllib.error.HTTPError as e: self._reply(out, e.code, e.read())
        except Exception: pass
    def _post(self, _u, url, body, out):
        try:
            req = urllib.request.Request(url.decode(), body, {"content-type": "application/json"})
            with urllib.request.urlopen(req, timeout=10) as r: self._reply(out, r.status, r.read())
        except urllib.error.HTTPError as e: self._reply(out, e.code, e.read())
        except Exception: pass
    def _download(self, _u, url, dest):
        try:
            with urllib.request.urlopen(url.decode(), timeout=10, context=CTX) as r, open(dest.decode(), "wb") as f:
                if r.status != 200: return 0
                shutil.copyfileobj(r, f)
            return 1
        except Exception: return 0
    def _stream_run(self, _u, url, ping, ctx):
        if self.aborted: return
        conn = http.client.HTTPConnection("127.0.0.1", API, timeout=None)
        try:
            conn.request("GET", url.decode().split(str(API), 1)[1]); resp = conn.getresponse(); self.stream = conn
            while not self.aborted:
                line = resp.fp.readline()
                if not line: break
                if line.startswith(b"event:"): ping(ctx)
        except Exception: pass
        finally: conn.close()
    def _abort(self, _u):
        self.aborted = True
        try: self.stream.sock.shutdown(2)
        except Exception: pass
    def _apply(self, _u, path):
        shutil.copy(path.decode(), self.installed / "counter-app")      # "install" = replace the app file
        return 1
    def _result(self, _u, outcome, detail, version):
        d, v = (detail or b"").decode(), (version or b"").decode()
        with self.cv: self.events.append((OUTCOMES[outcome], d, v)); self.cv.notify_all()
        say(f"   [{self.name}] {OUTCOMES[outcome]}" + (f" ({d})" if d else "") + (f" -> {v}" if v else ""))
    def wait(self, kind, timeout=20):
        end = time.time() + timeout
        with self.cv:
            while not any(e[0] == kind for e in self.events):
                if time.time() > end: return False
                self.cv.wait(0.2)
            return True
    def forget(self): 
        with self.cv: self.events.clear()
    def start(self): LIB.tri_client_start(self.c)
    def close(self): LIB.tri_client_stop(self.c); LIB.tri_client_destroy(self.c)

def admin(method, path, body=None):
    req = urllib.request.Request(BASE + path, json.dumps(body).encode() if body else None, {"content-type": "application/json", "authorization": f"Bearer {TOKEN}"}, method=method)
    with urllib.request.urlopen(req) as r: return json.loads(r.read())

def sign(*a):
    return subprocess.run(["node", str(ROOT / "tools/sign/sign.js"), *a], capture_output=True, text=True, env={**os.environ, "TRISKELION_URL": BASE, "ADMIN_TOKEN": TOKEN, "NODE_EXTRA_CA_CERTS": "/tmp/claude-0/cert/cert.pem"})

def main():
    work = Path(tempfile.mkdtemp(prefix="triskelion-demo-"))
    srv = subprocess.Popen(["node", "demo/server.mjs", str(API), str(BLOB), "/tmp/claude-0/cert", TOKEN], cwd=ROOT / "server", stdout=subprocess.PIPE, text=True)
    try:
        assert srv.stdout.readline().strip() == "READY"
        say("1. Setup: generate the signing key (stays offline) and register the app on the server")
        key = work / "counter.key"
        out = sign("keygen", str(key)).stdout; pub = json.loads(key.read_text())["publicKey"]
        admin("POST", "/admin/apps", {"id": "counter", "name": "Counter", "publicKey": pub})
        say(f"   public key embedded in both apps: {pub}\n")

        say("2. Two installed counter apps at v1.0.0 (Android + Windows) start up")
        android = CounterApp("android-counter", "android", "1.0.0", "aaaaaaaa-0000-4000-8000-0000000000a1", pub, work)
        windows = CounterApp("windows-counter", "windows", "1.0.0", "bbbbbbbb-0000-4000-8000-0000000000b1", pub, work)
        android.start(); windows.start()
        assert android.wait("UP_TO_DATE") and windows.wait("UP_TO_DATE")
        say("   -> nothing published yet, both up to date, both listening for broadcasts\n")
        android.forget(); windows.forget()

        say("3. You publish Android v1.1.0 (signed offline, uploaded, rolled out to 100%)")
        apk = work / "counter-1.1.0.apk"; apk.write_bytes(os.urandom(5000) + b"counter 1.1.0 android")
        r = sign("publish", "--key", str(key), "--app", "counter", "--platform", "android", "--version", "1.1.0", "--file", str(apk), "--changelog", "Bigger digits")
        say("   " + (r.stdout.strip().splitlines() or [r.stderr])[-1])
        assert android.wait("APPLIED"), "android should have been pinged, pulled and applied"
        say(f"   -> Android app was pinged, pulled, verified and installed ({(android.installed/'counter-app').stat().st_size} bytes)")
        time.sleep(1.5)
        say(f"   -> Windows app (different platform) got no update: events = {[e[0] for e in windows.events]}\n")

        say("4. Attack: someone replaces the Windows v1.1.0 file in the bucket after you published it")
        exe = work / "CounterApp-1.1.0.exe"; exe.write_bytes(os.urandom(6000) + b"counter 1.1.0 windows")
        # make the Windows app miss the broadcast so it has to find the release by pulling: stop it, publish, tamper, restart
        windows.close()
        r = sign("publish", "--key", str(key), "--app", "counter", "--platform", "windows", "--version", "1.1.0", "--file", str(exe))
        say("   " + (r.stdout.strip().splitlines() or [r.stderr])[-1])
        evil = exe.read_bytes()[:-1] + b"X"      # same size, one byte changed
        urllib.request.urlopen(urllib.request.Request(f"http://127.0.0.1:{BLOB+1}/counter/windows/1.1.0/CounterApp-1.1.0.exe", evil, method="PUT")).read()
        windows2 = CounterApp("windows-counter-2", "windows", "1.0.0", "bbbbbbbb-0000-4000-8000-0000000000b1", pub, work)
        windows2.start()
        assert windows2.wait("REJECTED"), "tampered file must be rejected"
        say(f"   -> tampered file REJECTED, nothing installed: {list(windows2.installed.iterdir())}\n")
        windows2.close()

        say("5. You re-upload the genuine file; the app retries and accepts it")
        urllib.request.urlopen(urllib.request.Request(f"http://127.0.0.1:{BLOB+1}/counter/windows/1.1.0/CounterApp-1.1.0.exe", exe.read_bytes(), method="PUT")).read()
        windows3 = CounterApp("windows-counter-3", "windows", "1.0.0", "bbbbbbbb-0000-4000-8000-0000000000b1", pub, work)
        windows3.start()
        assert windows3.wait("APPLIED")
        assert (windows3.installed / "counter-app").read_bytes() == exe.read_bytes()
        say("   -> installed file is byte-identical to what you signed\n")
        windows3.close()

        say("6. Admin view of what is out there")
        for row in admin("GET", "/admin/installs"): say(f"   {row}")
        say("\nDEMO PASSED")
        android.close(); windows.close() if False else None
    finally:
        srv.terminate(); shutil.rmtree(work, ignore_errors=True)

main()
