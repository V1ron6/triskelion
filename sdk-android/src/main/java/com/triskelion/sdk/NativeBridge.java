package com.triskelion.sdk;

/**
 * The only door into the native core. Kept in Java (not Kotlin) so the same file compiles against a
 * plain desktop JDK, where the JNI layer is tested (see jni-test/).
 *
 * Bodies cross as byte[] so UTF-8 survives untouched; strings that are ASCII or file paths cross as
 * String and are converted properly (not with JNI's modified UTF-8) on the native side.
 */
final class NativeBridge {
    static {
        System.loadLibrary("triskelion_jni");
    }

    private NativeBridge() {}

    /** Platform pieces the core calls. Called from the core's own threads, never the UI thread. */
    interface Host {
        /** null = transport failure. */
        HttpReply get(String url);

        HttpReply postJson(String url, byte[] body);

        /** Stream the url to destPath; true only for a complete 200. */
        boolean download(String url, String destPath);

        /** Block until the stream ends or streamAbort(); call ping(pingHandle) once per broadcast, only from inside this call. */
        void streamRun(String url, long pingHandle);

        /** Sticky: current and future streamRun calls return promptly. Callable from any thread. */
        void streamAbort();

        /** Called only with a verified file. true if the update was handed off or put in place. */
        boolean apply(String verifiedPath);

        void onResult(int outcome, String detail, String version);
    }

    static final class HttpReply {
        final int status;
        final byte[] body;

        HttpReply(int status, byte[] body) {
            this.status = status;
            this.body = body;
        }
    }

    /** Returns a handle, or 0 with a reason in errOut[0]. */
    static native long create(
            Host host,
            String serverUrl,
            String appId,
            String platform,
            String currentVersion,
            String publicKeyB64,
            String channel,
            String installId,
            String workDir,
            int checkIntervalMs,
            int retryIntervalMs,
            int reconnectDelayMs,
            boolean allowInsecureHttp,
            String[] errOut);

    static native void start(long handle);

    static native void checkNow(long handle);

    static native boolean updateApplied(long handle);

    /** Blocks until the core's threads end (bounded by the host's network timeouts). Not from the UI thread. */
    static native void stop(long handle);

    /** Stops first, then frees. Not from the UI thread, and never from inside a Host callback. */
    static native void destroy(long handle);

    /** Report a broadcast. Safe with a stale or unknown handle (ignored). */
    static native void ping(long pingHandle);
}
