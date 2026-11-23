package com.triskelion.sdk

import android.content.Context
import java.util.UUID

/** A random id created on first run, kept in app-private storage. Identifies the install, not the person or device. */
internal object InstallIdStore {
    private const val PREFS = "triskelion"
    private const val KEY = "install_id"

    @Synchronized
    fun get(context: Context): String {
        val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        prefs.getString(KEY, null)?.let { return it }
        val id = UUID.randomUUID().toString()
        prefs.edit().putString(KEY, id).apply()
        return id
    }
}
