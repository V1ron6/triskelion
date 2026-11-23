package com.triskelion.sdk

import android.content.Context
import android.util.Log
import java.io.File
import java.net.HttpURLConnection
import java.net.URL

/**
 * The platform pieces the native core calls: HTTP, the broadcast stream and the install hand-off.
 * Every method runs on a core thread, never the UI thread.
 */
internal class AndroidHost(
    private val context: Context,
    private val onOutcome: (Int, String, String) -> Unit,
) : NativeBridge.Host {

    companion object {
        private const val TAG = "Triskelion"
        private const val CONNECT_MS = 15_000
        private const val READ_MS = 30_000
        // The server sends nothing between updates, so a quiet stream is normal. Reconnect now and then
        // so a half-dead connection (NAT timeout, network change) cannot hide updates for long.
        private const val STREAM_READ_MS = 5 * 60_000
        private const val MAX_JSON_BYTES = 256 * 1024
    }

    @Volatile private var aborted = false
    @Volatile private var streamConn: HttpURLConnection? = null

    private fun open(url: String, readMs: Int): HttpURLConnection =
        (URL(url).openConnection() as HttpURLConnection).apply {
            connectTimeout = CONNECT_MS
            readTimeout = readMs
            instanceFollowRedirects = false // an https artifact URL must not bounce to somewhere else silently
            setRequestProperty("User-Agent", "triskelion-android")
        }

    private fun readCapped(conn: HttpURLConnection): ByteArray {
        val stream = if (conn.responseCode in 200..299) conn.inputStream else conn.errorStream
        if (stream == null) return ByteArray(0)
        stream.use { s ->
            val out = java.io.ByteArrayOutputStream()
            val buf = ByteArray(8192)
            while (true) {
                val n = s.read(buf)
                if (n < 0) break
                out.write(buf, 0, n)
                if (out.size() > MAX_JSON_BYTES) return ByteArray(0)
            }
            return out.toByteArray()
        }
    }

    override fun get(url: String): NativeBridge.HttpReply? = try {
        val conn = open(url, READ_MS)
        try { NativeBridge.HttpReply(conn.responseCode, readCapped(conn)) } finally { conn.disconnect() }
    } catch (e: Exception) {
        Log.w(TAG, "GET failed: ${e.javaClass.simpleName}")
        null
    }

    override fun postJson(url: String, body: ByteArray): NativeBridge.HttpReply? = try {
        val conn = open(url, READ_MS).apply {
            requestMethod = "POST"
            doOutput = true
            setRequestProperty("Content-Type", "application/json")
            setFixedLengthStreamingMode(body.size)
        }
        try {
            conn.outputStream.use { it.write(body) }
            NativeBridge.HttpReply(conn.responseCode, readCapped(conn))
        } finally { conn.disconnect() }
    } catch (e: Exception) {
        Log.w(TAG, "POST failed: ${e.javaClass.simpleName}")
        null
    }

    override fun download(url: String, destPath: String): Boolean = try {
        val conn = open(url, READ_MS)
        try {
            if (conn.responseCode != 200) false else {
                conn.inputStream.use { input -> File(destPath).outputStream().use { input.copyTo(it) } }
                true
            }
        } finally { conn.disconnect() }
    } catch (e: Exception) {
        Log.w(TAG, "download failed: ${e.javaClass.simpleName}")
        false
    }

    override fun streamRun(url: String, pingHandle: Long) {
        if (aborted) return
        try {
            val conn = open(url, STREAM_READ_MS)
            streamConn = conn
            if (aborted) { conn.disconnect(); return } // abort arrived while connecting
            try {
                if (conn.responseCode != 200) return
                conn.inputStream.bufferedReader(Charsets.UTF_8).use { reader ->
                    while (!aborted) {
                        val line = reader.readLine() ?: break
                        // Only the fact that something was broadcast matters; its content is never trusted.
                        if (line.startsWith("event:")) NativeBridge.ping(pingHandle)
                    }
                }
            } finally { conn.disconnect(); streamConn = null }
        } catch (e: Exception) {
            if (!aborted) Log.d(TAG, "stream ended: ${e.javaClass.simpleName}")
        }
    }

    override fun streamAbort() {
        aborted = true
        streamConn?.disconnect() // unblocks the reader thread
    }

    override fun apply(verifiedPath: String): Boolean = ApkInstaller.install(context, File(verifiedPath))

    override fun onResult(outcome: Int, detail: String, version: String) {
        Log.i(TAG, "result=$outcome detail=$detail version=$version")
        try { onOutcome(outcome, detail, version) } catch (e: Exception) { Log.w(TAG, "listener threw", e) }
    }
}
