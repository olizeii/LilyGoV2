package de.olizei.watchbridge

import android.content.Context
import androidx.core.content.edit

/** Einstellungen (SharedPreferences). Das Untis-Passwort bleibt nur auf dem Gerät. */
class Prefs(ctx: Context) {
    private val sp = ctx.getSharedPreferences("watchbridge", Context.MODE_PRIVATE)

    var watchAddress: String?
        get() = sp.getString("watch_addr", null)
        set(v) = sp.edit { putString("watch_addr", v) }

    var raybanAddress: String?
        get() = sp.getString("rayban_addr", null)
        set(v) = sp.edit { putString("rayban_addr", v) }

    var forwardNotifications: Boolean
        get() = sp.getBoolean("fwd_notif", true)
        set(v) = sp.edit { putBoolean("fwd_notif", v) }

    /** Dienst läuft (auch nach Neustart wieder starten). */
    var autoStart: Boolean
        get() = sp.getBoolean("auto_start", false)
        set(v) = sp.edit { putBoolean("auto_start", v) }

    var untisServer: String
        get() = sp.getString("untis_server", "") ?: ""
        set(v) = sp.edit { putString("untis_server", v.trim()) }

    var untisSchool: String
        get() = sp.getString("untis_school", "") ?: ""
        set(v) = sp.edit { putString("untis_school", v.trim()) }

    var untisUser: String
        get() = sp.getString("untis_user", "") ?: ""
        set(v) = sp.edit { putString("untis_user", v.trim()) }

    var untisPassword: String
        get() = sp.getString("untis_pw", "") ?: ""
        set(v) = sp.edit { putString("untis_pw", v) }

    /** Claude-API-Schlüssel (console.anthropic.com). Bleibt nur auf dem Gerät. */
    var aiKey: String
        get() = sp.getString("ai_key", "") ?: ""
        set(v) = sp.edit { putString("ai_key", v.trim()) }

    /** Claude-Modell für den KI-Assistenten (Standard: Haiku 5.5 – schnell und günstig). */
    var aiModel: String
        get() = sp.getString("ai_model", "claude-haiku-5-5") ?: "claude-haiku-5-5"
        set(v) = sp.edit { putString("ai_model", v) }

    /** Eigene Befehle für Tasker/MacroDroid, eine Zeile pro Befehl: "name: was er tut". */
    var aiCommands: String
        get() = sp.getString("ai_commands", "") ?: ""
        set(v) = sp.edit { putString("ai_commands", v) }

    val untisConfigured: Boolean
        get() = untisServer.isNotBlank() && untisSchool.isNotBlank() && untisUser.isNotBlank()
}
