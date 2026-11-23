package com.triskelion.sdk

import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInstaller
import android.os.Build
import android.util.Log
import java.io.File

/**
 * Hands a VERIFIED apk to the system installer. Android shows its own confirmation (or installs silently
 * where the OS allows it for an update from the same installer). The result comes back to
 * [InstallResultReceiver].
 */
internal object ApkInstaller {
    private const val TAG = "Triskelion"
    const val ACTION = "com.triskelion.sdk.INSTALL_RESULT"

    fun install(context: Context, apk: File): Boolean {
        val installer = context.packageManager.packageInstaller
        val params = PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL)
        params.setSize(apk.length())
        if (Build.VERSION.SDK_INT >= 31) {
            // Lets Android 12+ finish a same-installer update without a prompt when it is permitted.
            params.setRequireUserAction(PackageInstaller.SessionParams.USER_ACTION_NOT_REQUIRED)
        }
        var session: PackageInstaller.Session? = null
        return try {
            val id = installer.createSession(params)
            session = installer.openSession(id)
            apk.inputStream().use { input ->
                session.openWrite("update.apk", 0, apk.length()).use { out ->
                    input.copyTo(out)
                    session.fsync(out)
                }
            }
            val intent = Intent(ACTION).setPackage(context.packageName)
            // The system fills in extras on this intent, so it must be mutable on Android 12+.
            val flags = PendingIntent.FLAG_UPDATE_CURRENT or
                if (Build.VERSION.SDK_INT >= 31) PendingIntent.FLAG_MUTABLE else 0
            val pending = PendingIntent.getBroadcast(context, id, intent, flags)
            session.commit(pending.intentSender)
            true
        } catch (e: Exception) {
            Log.w(TAG, "install hand-off failed: ${e.javaClass.simpleName}: ${e.message}")
            try { session?.abandon() } catch (_: Exception) {}
            false
        } finally {
            session?.close()
        }
    }
}
