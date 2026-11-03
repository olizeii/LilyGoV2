package de.olizei.watchbridge

import android.app.Notification
import android.service.notification.StatusBarNotification
import android.util.Log
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

/**
 * Protokoll-Logik zwischen Handy und Uhr (siehe README "BLE-Protokoll").
 * Die Datenquellen (Medien, Akku, Karte, Untis) hängt der [BridgeService] ein.
 */
class Bridge(private val app: WatchBridgeApp) {

    var media: MediaWatcher? = null
    var battery: BatteryWatcher? = null
    var maps: MapProvider? = null
    var untis: UntisSync? = null

    private val _log = MutableStateFlow("")
    /** Letzte Aktion – nur zur Anzeige in der App. */
    val log: StateFlow<String> = _log

    private val lastNotifText = HashMap<String, String>()
    private var lastNav = ""

    init {
        app.link.onMessage = { msg -> app.scope.launch { handle(msg) } }
        app.notes.onChanged = { syncNotes() }
    }

    // --- Senden -------------------------------------------------------------------

    /** Einzelfeld säubern: '|' trennt Felder, Zeilenumbrüche nur im letzten Feld erlaubt. */
    private fun f(s: CharSequence?): String =
        (s ?: "").toString().replace('|', '/').replace('\n', ' ').replace('\r', ' ').trim()

    private fun last(s: CharSequence?): String = (s ?: "").toString().replace("\r", "").trim()

    fun send(topic: String, vararg fields: String) {
        app.link.send(if (fields.isEmpty()) topic else topic + "|" + fields.joinToString("|"))
    }

    fun sendRaw(message: String) = app.link.send(message)

    // --- Befehle der Uhr ---------------------------------------------------------

    private fun handle(raw: String) {
        val cmd = raw.substringBefore('|').trim().uppercase()
        val arg = raw.substringAfter('|', "")
        _log.value = "Uhr: $cmd"
        when (cmd) {
            "SYNC" -> syncAll()
            "MEDIA" -> media?.command(arg)
            "UNTIS_REQ" -> untis?.refresh(force = false)
            "MAP_REQ" -> {
                val p = arg.split('|')
                maps?.lease(p.getOrNull(0)?.toIntOrNull() ?: 15, p.getOrNull(1) == "1")
            }
            "MAP_STOP" -> maps?.endLease()
            "NOTIF_DISMISS" -> NotifListener.instance?.dismiss(arg)
            // Alles andere an Tasker/MacroDroid weiterreichen (Intent-Empfang "de.olizei.watchbridge.WATCH")
            else -> app.sendBroadcast(
                android.content.Intent(ACTION_FROM_WATCH).putExtra("cmd", cmd).putExtra("arg", arg).putExtra("msg", raw),
            ).also { Log.d(TAG, "An Tasker: $raw") }
        }
    }

    /** Uhr hat (neu) verbunden -> alles schicken, was sie anzeigt. */
    fun syncAll() {
        sendTime()
        battery?.sendAll()
        media?.publish(force = true)
        untis?.sendCached()
        untis?.refresh(force = false)
        syncNotes()
        maps?.sendLast()
        // Duplikatsperren zurücksetzen: die Uhr kennt nach dem Verbinden nichts davon
        lastNotifText.clear()
        lastNav = ""
        NotifListener.instance?.resendActive()  // schickt auch eine laufende Navigation
        if (lastNav.isEmpty()) send("NAV_END")
        _log.value = "Synchronisiert"
    }

    fun sendTime() = send("TIME", (System.currentTimeMillis() / 1000).toString())

    fun syncNotes() {
        val list = app.notes.notes.value.filter { it.onWatch }
        send("NOTES_BEGIN")
        for (n in list) send("NOTE", f(n.id), f(n.title), last(n.body).take(3000))
        send("NOTES_END")
    }

    // --- Benachrichtigungen ---------------------------------------------------------

    /** @param silent true = beim Wiederverbinden nachgeliefert -> Uhr soll nicht vibrieren */
    fun onNotificationPosted(sbn: StatusBarNotification, appLabel: String, silent: Boolean = false) {
        val n = sbn.notification
        if (isNavigation(sbn)) {
            sendNav(n)
            return
        }
        if (!app.prefs.forwardNotifications || !isForwardable(sbn)) return

        val ex = n.extras
        val title = ex.getCharSequence(Notification.EXTRA_TITLE)
            ?: ex.getCharSequence(Notification.EXTRA_CONVERSATION_TITLE)
        // Messenger: letzte Nachricht statt Zusammenfassung ("3 neue Nachrichten")
        val messages = ex.getParcelableArray(Notification.EXTRA_MESSAGES)
        val lastMsg = messages?.lastOrNull()?.let { (it as? android.os.Bundle)?.getCharSequence("text") }
        val text = lastMsg ?: ex.getCharSequence(Notification.EXTRA_BIG_TEXT) ?: ex.getCharSequence(Notification.EXTRA_TEXT)
        if (title.isNullOrBlank() && text.isNullOrBlank()) return

        val body = last(text).take(600)
        // Gleiche Notification mit gleichem Text (z.B. Update ohne Änderung) nicht doppelt
        if (lastNotifText[sbn.key] == "$title$body") return
        lastNotifText[sbn.key] = "$title$body"
        send(if (silent) "NOTIF_S" else "NOTIF", f(sbn.key), f(appLabel), f(title), body)
    }

    fun onNotificationRemoved(sbn: StatusBarNotification) {
        if (isNavigation(sbn)) {
            lastNav = ""
            send("NAV_END")
            return
        }
        if (lastNotifText.remove(sbn.key) != null) send("NOTIF_DEL", f(sbn.key))
    }

    private fun isForwardable(sbn: StatusBarNotification): Boolean {
        val n = sbn.notification
        if (sbn.packageName == app.packageName) return false
        if (sbn.isOngoing || !sbn.isClearable) return false
        if (n.flags and Notification.FLAG_GROUP_SUMMARY != 0) return false
        if (n.extras.containsKey(Notification.EXTRA_MEDIA_SESSION)) return false  // Spotify & Co.
        return n.category !in setOf(
            Notification.CATEGORY_TRANSPORT, Notification.CATEGORY_PROGRESS,
            Notification.CATEGORY_SERVICE, Notification.CATEGORY_SYSTEM,
        )
    }

    // --- Navigation (Google Maps) ------------------------------------------------------

    private fun isNavigation(sbn: StatusBarNotification): Boolean =
        sbn.packageName == MAPS_PACKAGE &&
            (sbn.notification.category == Notification.CATEGORY_NAVIGATION || sbn.isOngoing)

    private fun sendNav(n: Notification) {
        val ex = n.extras
        val title = f(ex.getCharSequence(Notification.EXTRA_TITLE))  // meist Distanz, z.B. "300 m"
        val text = f(ex.getCharSequence(Notification.EXTRA_TEXT))    // Anweisung / Straße
        val sub = f(ex.getCharSequence(Notification.EXTRA_SUB_TEXT)) // Ankunft, Restzeit
        if (title.isEmpty() && text.isEmpty()) return

        // Manche Versionen packen Distanz + Anweisung in den Titel ("300 m · Rechts abbiegen")
        var distance = title
        var instruction = text
        val dot = title.indexOf(" · ")
        if (instruction.isEmpty() && dot > 0) {
            distance = title.substring(0, dot)
            instruction = title.substring(dot + 3)
        }
        val maneuver = guessManeuver("$title $text")
        val state = "$maneuver|$distance|$instruction|$sub"
        if (state == lastNav) return
        lastNav = state
        send("NAV", maneuver, distance, instruction, sub)
    }

    companion object {
        private const val TAG = "Bridge"
        const val MAPS_PACKAGE = "com.google.android.apps.maps"
        const val ACTION_FROM_WATCH = "de.olizei.watchbridge.WATCH"

        /** Manöver aus dem Anweisungstext erraten (Deutsch + Englisch). */
        fun guessManeuver(s: String): String {
            val t = s.lowercase()
            fun has(vararg w: String) = w.any { t.contains(it) }
            return when {
                has("wenden", "kehrtwende", "u-turn", "make a u") -> "uturn"
                has("kreisverkehr", "roundabout") -> "roundabout"
                has("ziel", "angekommen", "destination", "arrive") -> "arrive"
                has("scharf links", "sharp left") -> "sharp_left"
                has("scharf rechts", "sharp right") -> "sharp_right"
                has("leicht links", "links halten", "slight left", "keep left") -> "slight_left"
                has("leicht rechts", "rechts halten", "slight right", "keep right") -> "slight_right"
                has("links", "left") -> "left"
                has("rechts", "right") -> "right"
                else -> "straight"
            }
        }
    }
}
