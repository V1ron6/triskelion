package com.triskelion.sdk

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInstaller
import android.content.pm.PackageManager
import android.os.Build
import android.util.Log
import androidx.core.app.NotificationCompat

/** Receives the system installer's verdict. When Android needs the user to confirm, shows a notification that opens the prompt. */
internal class InstallResultReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE)
        when (status) {
            PackageInstaller.STATUS_PENDING_USER_ACTION -> {
                @Suppress("DEPRECATION")
                val confirm: Intent? = if (Build.VERSION.SDK_INT >= 33)
                    intent.getParcelableExtra(Intent.EXTRA_INTENT, Intent::class.java)
                else intent.getParcelableExtra(Intent.EXTRA_INTENT)
                if (confirm != null) askUser(context, confirm)
            }
            PackageInstaller.STATUS_SUCCESS -> Log.i("Triskelion", "update installed")
            else -> Log.w("Triskelion", "install failed: status=$status ${intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE)}")
        }
    }

    private fun askUser(context: Context, confirm: Intent) {
        confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        val canNotify = Build.VERSION.SDK_INT < 33 ||
            context.checkSelfPermission("android.permission.POST_NOTIFICATIONS") == PackageManager.PERMISSION_GRANTED
        if (!canNotify) {
            // No notification permission: the best we can do is open the prompt (works if the app is in front).
            try { context.startActivity(confirm) } catch (e: Exception) { Log.w("Triskelion", "cannot show install prompt", e) }
            return
        }
        val nm = context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        if (Build.VERSION.SDK_INT >= 26) {
            nm.createNotificationChannel(NotificationChannel("triskelion_updates", "App updates", NotificationManager.IMPORTANCE_DEFAULT))
        }
        val flags = PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        val open = PendingIntent.getActivity(context, 0, confirm, flags)
        val label = context.applicationInfo.loadLabel(context.packageManager)
        val n = NotificationCompat.Builder(context, "triskelion_updates")
            .setSmallIcon(context.applicationInfo.icon)
            .setContentTitle("$label update ready")
            .setContentText("Tap to install the new version")
            .setContentIntent(open)
            .setAutoCancel(true)
            .build()
        nm.notify(0x7215, n)
    }
}
