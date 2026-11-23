package com.triskelion.sdk

import android.content.Context
import android.util.Log
import com.google.firebase.messaging.FirebaseMessaging
import org.json.JSONObject
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.Executors

/**
 * Keeps this app up to date from your Triskelion server.
 *
 *     class MyApp : Application() {
 *         override fun onCreate() {
 *             super.onCreate()
 *             Triskelion.init(this, Triskelion.Config(
 *                 serverUrl = "https://triskelion.onrender.com",
 *                 appId = "myapp",
 *                 publicKeyB64 = "<your PUBLIC key>",
 *             ))
 *         }
 *     }
 *
 * Call it from Application.onCreate (not an Activity) so that a push that wakes the process also finds it ready.
 */
object Triskelion {
    private const val TAG = "Triskelion"

    class Config(
        val serverUrl: String,
        val appId: String,
        /** The PUBLIC Ed25519 key (base64) printed by `sign.js keygen`. Never the private key. */
        val publicKeyB64: String,
        val channel: String = "stable",
        /** Defaults to the app's versionName, which must look like 1.2.3. */
        val versionName: String? = null,
        /** Register for Firebase pushes so a backgrounded app is told about updates. Needs google-services.json. */
        val useFcm: Boolean = true,
        /** Optional: called on a background thread with every result (see [Outcome]). */
        val onResult: ((Outcome, String, String) -> Unit)? = null,
    )

    enum class Outcome { UP_TO_DATE, APPLIED, CHECK_FAILED, DOWNLOAD_FAILED, REJECTED, APPLY_FAILED;
        companion object { fun from(code: Int) = values().getOrElse(code) { CHECK_FAILED } }
    }

    private val worker = Executors.newSingleThreadExecutor { r -> Thread(r, "triskelion-control").apply { isDaemon = true } }
    private val lock = Any()
    private var handle = 0L
    private var host: AndroidHost? = null
    private var config: Config? = null
    private var version = ""
    private var installId = ""

    /** Starts the updater. Safe to call more than once; later calls are ignored. Never blocks the caller. */
    fun init(context: Context, config: Config) {
        val app = context.applicationContext
        synchronized(lock) {
            if (this.config != null) return
            this.config = config
        }
        worker.execute {
            val v = config.versionName ?: try {
                app.packageManager.getPackageInfo(app.packageName, 0).versionName ?: ""
            } catch (e: Exception) { "" }
            val id = InstallIdStore.get(app)
            val work = File(app.noBackupFilesDir, "triskelion").apply { mkdirs() }
            // Leftovers from an interrupted run are never trusted again.
            work.listFiles()?.forEach { it.delete() }

            val h = AndroidHost(app) { code, detail, ver -> config.onResult?.invoke(Outcome.from(code), detail, ver) }
            val err = arrayOfNulls<String>(1)
            val handle = NativeBridge.create(h, config.serverUrl, config.appId, "android", v, config.publicKeyB64,
                config.channel, id, work.absolutePath, 0, 0, 0, false, err)
            if (handle == 0L) {
                Log.e(TAG, "not started: ${err[0]}")
                synchronized(lock) { this.config = null }
                return@execute
            }
            synchronized(lock) { this.handle = handle; this.host = h; this.version = v; this.installId = id }
            NativeBridge.start(handle)
            if (config.useFcm) fetchToken(app)
        }
    }

    /** Ask the server now (e.g. from a "check for updates" button). */
    fun checkNow() {
        synchronized(lock) { if (handle != 0L) NativeBridge.checkNow(handle) }
    }

    /** Stops the updater and releases its threads. Normally not needed. */
    fun shutdown() {
        worker.execute {
            val h: Long
            synchronized(lock) { h = handle; handle = 0; host = null; config = null }
            if (h != 0L) { NativeBridge.stop(h); NativeBridge.destroy(h) }
        }
    }

    // ---- push token (FCM) ------------------------------------------------------------------------

    internal fun appId(): String? = synchronized(lock) { config?.appId }

    private fun fetchToken(context: Context) {
        try {
            FirebaseMessaging.getInstance().token.addOnSuccessListener { token -> registerToken(token) }
        } catch (e: Exception) {
            Log.w(TAG, "FCM unavailable: ${e.javaClass.simpleName}")
        }
    }

    /** Tell the server which push token belongs to this install. The core registers the install itself; this adds the token. */
    internal fun registerToken(token: String) {
        val cfg: Config; val ver: String; val id: String
        synchronized(lock) { cfg = config ?: return; ver = version; id = installId }
        if (ver.isEmpty() || id.isEmpty() || !cfg.serverUrl.startsWith("https://")) return
        worker.execute {
            try {
                val body = JSONObject()
                    .put("install", id).put("app", cfg.appId).put("platform", "android")
                    .put("version", ver).put("channel", cfg.channel).put("pushToken", token)
                    .toString().toByteArray(Charsets.UTF_8)
                val conn = URL(cfg.serverUrl.trimEnd('/') + "/v1/register").openConnection() as HttpURLConnection
                try {
                    conn.connectTimeout = 15_000; conn.readTimeout = 30_000
                    conn.requestMethod = "POST"; conn.doOutput = true
                    conn.setRequestProperty("Content-Type", "application/json")
                    conn.setFixedLengthStreamingMode(body.size)
                    conn.outputStream.use { it.write(body) }
                    if (conn.responseCode !in 200..299) Log.w(TAG, "token registration: http ${conn.responseCode}")
                } finally { conn.disconnect() }
            } catch (e: Exception) {
                Log.w(TAG, "token registration failed: ${e.javaClass.simpleName}")
            }
        }
    }
}
