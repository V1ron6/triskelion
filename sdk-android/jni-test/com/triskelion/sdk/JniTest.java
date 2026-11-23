package com.triskelion.sdk;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;

/** Exercises the JNI bridge end to end against a fake host. Exit code 0 only if every check passes. */
public final class JniTest {
    static int checks = 0, failures = 0;

    static void check(boolean ok, String what) {
        checks++;
        if (!ok) {
            failures++;
            System.out.println("FAIL: " + what);
        }
    }

    static final String KEY = "d3KuDPeA1oGR+J0+gsVVLHbjUgWcPG/epeKBtzwUBtM=";
    static final String INSTALL = "aaaaaaaa-0000-4000-8000-000000000001";
    static Path fixtures;
    static byte[] artifact;
    static String updateBody, upToDateBody = "{\"update\":false}";

    static class FakeHost implements NativeBridge.Host {
        final Object lock = new Object();
        String checkBody = upToDateBody;
        int checkStatus = 200;
        boolean throwEverywhere = false;
        boolean aborted = false;
        long pingHandle = 0;
        int gets, posts, downloads, applies, streamRuns;
        String lastGetUrl, lastPostBody, appliedPath;
        byte[] corruptArtifact = null;
        final List<String> results = new ArrayList<>();

        void maybeThrow() { if (throwEverywhere) throw new IllegalStateException("host boom"); }

        public NativeBridge.HttpReply get(String url) {
            maybeThrow();
            synchronized (lock) { gets++; lastGetUrl = url; return new NativeBridge.HttpReply(checkStatus, checkBody.getBytes(StandardCharsets.UTF_8)); }
        }
        public NativeBridge.HttpReply postJson(String url, byte[] body) {
            maybeThrow();
            synchronized (lock) { posts++; lastPostBody = new String(body, StandardCharsets.UTF_8); }
            return new NativeBridge.HttpReply(200, "{\"ok\":true}".getBytes(StandardCharsets.UTF_8));
        }
        public boolean download(String url, String dest) {
            maybeThrow();
            try {
                byte[] b;
                synchronized (lock) { downloads++; b = corruptArtifact != null ? corruptArtifact : artifact; }
                Files.write(Paths.get(dest), b);
                return true;
            } catch (IOException e) { return false; }
        }
        public void streamRun(String url, long handle) {
            maybeThrow();
            synchronized (lock) {
                streamRuns++;
                pingHandle = handle;
                lock.notifyAll();
                while (!aborted) { try { lock.wait(); } catch (InterruptedException e) { return; } }
            }
        }
        public void streamAbort() { synchronized (lock) { aborted = true; lock.notifyAll(); } }
        public boolean apply(String path) {
            maybeThrow();
            synchronized (lock) { applies++; appliedPath = path; return true; }
        }
        public void onResult(int outcome, String detail, String version) {
            synchronized (lock) { results.add(outcome + "|" + detail + "|" + version); lock.notifyAll(); }
        }

        boolean waitFor(java.util.function.BooleanSupplier cond, long ms) {
            long end = System.currentTimeMillis() + ms;
            synchronized (lock) {
                while (!cond.getAsBoolean()) {
                    long left = end - System.currentTimeMillis();
                    if (left <= 0) return false;
                    try { lock.wait(left); } catch (InterruptedException e) { return false; }
                }
                return true;
            }
        }
    }

    static long create(FakeHost h, String workDir, String[] err, String server, String install) {
        return NativeBridge.create(h, server, "locked", "android", "1.0.0", KEY, "stable", install, workDir,
                60_000, 60_000, 100, false, err);
    }

    static Path tmp(String name) throws IOException { return Files.createTempDirectory(name); }

    public static void main(String[] args) throws Exception {
        fixtures = Paths.get(System.getProperty("tri.fixtures"));
        artifact = Files.readAllBytes(fixtures.resolve("artifact.bin"));
        updateBody = new String(Files.readAllBytes(fixtures.resolve("check_response.json")), StandardCharsets.UTF_8);

        fullFlowWithPing();
        invalidConfig();
        hostThatThrows();
        rejectedArtifact();
        unicodeWorkDir();
        staleHandles();
        lifecycle();

        System.out.println(checks + " checks, " + failures + " failures");
        System.exit(failures == 0 ? 0 : 1);
    }

    static void fullFlowWithPing() throws Exception {
        FakeHost h = new FakeHost();
        String[] err = new String[1];
        long c = create(h, tmp("tri-flow").toString(), err, "https://svc.example.com", INSTALL);
        check(c != 0, "create succeeds: " + err[0]);
        check(!NativeBridge.updateApplied(c), "not applied yet");
        NativeBridge.start(c);
        check(h.waitFor(() -> h.results.size() >= 1, 5000), "first result arrives");
        synchronized (h.lock) {
            check(h.results.get(0).startsWith("0|"), "up to date first: " + h.results.get(0));
            check(h.lastGetUrl.equals("https://svc.example.com/v1/check?app=locked&platform=android&version=1.0.0&install=" + INSTALL + "&channel=stable"), "check url: " + h.lastGetUrl);
            check(h.posts >= 1 && h.lastPostBody.contains(INSTALL), "registered");
        }
        check(h.waitFor(() -> h.pingHandle != 0, 5000), "stream started with a ping handle");

        synchronized (h.lock) { h.checkBody = updateBody; }
        NativeBridge.ping(h.pingHandle);
        check(h.waitFor(() -> h.applies == 1, 5000), "ping triggers pull and apply");
        check(h.waitFor(() -> NativeBridge.updateApplied(c), 5000), "updateApplied becomes true");
        synchronized (h.lock) {
            check(h.results.get(h.results.size() - 1).startsWith("1|"), "applied outcome: " + h.results);
            check(h.appliedPath != null && Files.exists(Paths.get(h.appliedPath)), "applied file exists at hand-off");
        }
        NativeBridge.stop(c);
        NativeBridge.stop(c); // idempotent
        NativeBridge.destroy(c);
        System.gc();
    }

    static void invalidConfig() {
        FakeHost h = new FakeHost();
        String[] err = new String[1];
        check(create(h, "/tmp", err, "http://svc.example.com", INSTALL) == 0 && err[0] != null && !err[0].isEmpty(), "http server rejected: " + err[0]);
        err[0] = null;
        check(create(h, "/tmp", err, "https://svc.example.com", "short") == 0 && err[0] != null, "bad install id rejected: " + err[0]);
        check(NativeBridge.create(null, "https://s.example.com", "locked", "android", "1.0.0", KEY, "stable", INSTALL, "/tmp", 0, 0, 0, false, new String[1]) == 0, "null host rejected");
        check(NativeBridge.create(h, "http://s.example.com", "locked", "android", "1.0.0", KEY, "stable", INSTALL, "/tmp", 0, 0, 0, false, null) == 0, "null errOut tolerated");
        // zero handle is a no-op everywhere
        NativeBridge.start(0); NativeBridge.checkNow(0); NativeBridge.stop(0); NativeBridge.destroy(0); NativeBridge.ping(0);
        check(!NativeBridge.updateApplied(0), "zero handle not applied");
    }

    static void hostThatThrows() throws Exception {
        FakeHost h = new FakeHost();
        h.throwEverywhere = true;
        String[] err = new String[1];
        long c = create(h, tmp("tri-throw").toString(), err, "https://svc.example.com", INSTALL);
        check(c != 0, "create with throwing host");
        NativeBridge.start(c);
        check(h.waitFor(() -> h.results.size() >= 1, 5000), "failure reported even though host throws");
        synchronized (h.lock) { check(h.results.get(0).startsWith("2|"), "check failed outcome: " + h.results.get(0)); }
        NativeBridge.stop(c); // the stream thread: streamRun threw, so it must still be able to stop
        NativeBridge.destroy(c);
    }

    static void rejectedArtifact() throws Exception {
        FakeHost h = new FakeHost();
        h.checkBody = updateBody;
        h.corruptArtifact = new byte[4096]; // right size, wrong hash
        String[] err = new String[1];
        long c = create(h, tmp("tri-rej").toString(), err, "https://svc.example.com", INSTALL);
        NativeBridge.start(c);
        check(h.waitFor(() -> h.results.size() >= 1, 5000), "result arrives");
        synchronized (h.lock) {
            check(h.results.get(0).startsWith("4|"), "tampered artifact rejected: " + h.results.get(0));
            check(h.applies == 0, "never applied");
        }
        check(!NativeBridge.updateApplied(c), "not marked applied");
        NativeBridge.stop(c);
        NativeBridge.destroy(c);
    }

    static void unicodeWorkDir() throws Exception {
        FakeHost h = new FakeHost();
        h.checkBody = updateBody;
        Path base = tmp("tri-uni");
        Path dir = base.resolve("work-é☕🚀");
        Files.createDirectories(dir);
        String[] err = new String[1];
        long c = create(h, dir.toString(), err, "https://svc.example.com", INSTALL);
        check(c != 0, "create with unicode work dir: " + err[0]);
        NativeBridge.start(c);
        check(h.waitFor(() -> h.applies == 1, 5000), "applied with unicode path");
        synchronized (h.lock) { check(h.appliedPath != null && h.appliedPath.startsWith(dir.toString()), "path round-trips: " + h.appliedPath); }
        NativeBridge.stop(c);
        NativeBridge.destroy(c);
    }

    static void staleHandles() throws Exception {
        FakeHost h = new FakeHost();
        String[] err = new String[1];
        long c = create(h, tmp("tri-stale").toString(), err, "https://svc.example.com", INSTALL);
        NativeBridge.start(c);
        check(h.waitFor(() -> h.pingHandle != 0, 5000), "have a ping handle");
        long handle = h.pingHandle;
        NativeBridge.stop(c);
        NativeBridge.destroy(c);
        NativeBridge.ping(handle);        // stream is gone: must be ignored, not crash
        NativeBridge.ping(handle + 8);    // never existed
        NativeBridge.ping(-1);
        check(true, "stale pings survived");
    }

    static void lifecycle() throws Exception {
        for (int i = 0; i < 25; i++) {
            FakeHost h = new FakeHost();
            String[] err = new String[1];
            long c = create(h, tmp("tri-life").toString(), err, "https://svc.example.com", INSTALL);
            if (i % 3 != 0) NativeBridge.start(c);
            if (i % 2 == 0) NativeBridge.checkNow(c);
            NativeBridge.destroy(c);
        }
        check(true, "25 create/destroy cycles");
        // many threads pinging concurrently while the stream is torn down
        FakeHost h = new FakeHost();
        String[] err = new String[1];
        long c = create(h, tmp("tri-conc").toString(), err, "https://svc.example.com", INSTALL);
        NativeBridge.start(c);
        h.waitFor(() -> h.pingHandle != 0, 5000);
        final long handle = h.pingHandle;
        Thread[] ts = new Thread[4];
        for (int i = 0; i < ts.length; i++) {
            ts[i] = new Thread(() -> { for (int k = 0; k < 2000; k++) NativeBridge.ping(handle); });
            ts[i].start();
        }
        NativeBridge.stop(c);
        for (Thread t : ts) t.join();
        NativeBridge.destroy(c);
        check(true, "concurrent pings during stop");
    }
}
