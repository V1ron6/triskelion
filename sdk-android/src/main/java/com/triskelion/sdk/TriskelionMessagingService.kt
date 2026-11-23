package com.triskelion.sdk

import com.google.firebase.messaging.FirebaseMessagingService
import com.google.firebase.messaging.RemoteMessage

/**
 * Wakes the updater when the server says a release exists. The message carries no update and is never
 * trusted for content: it only makes the app run its normal, signature-verified pull.
 */
class TriskelionMessagingService : FirebaseMessagingService() {
    override fun onMessageReceived(message: RemoteMessage) {
        val data = message.data
        if (data["triskelion"] == "update" && data["app"] == Triskelion.appId()) Triskelion.checkNow()
    }

    override fun onNewToken(token: String) {
        Triskelion.registerToken(token)
    }
}
