package de.olizei.watchbridge

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.media.AudioManager
import android.media.MediaMetadata
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.media.session.PlaybackState
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.provider.MediaStore
import org.json.JSONArray
import org.json.JSONObject

/**
 * Liest die aktive Medien-Session (Spotify bevorzugt) und steuert sie.
 * Braucht den Benachrichtigungszugriff (NotificationListener) – sonst SecurityException.
 */
class MediaWatcher(private val ctx: Context, private val bridge: Bridge) {

    private val msm = ctx.getSystemService(MediaSessionManager::class.java)
    private val audio = ctx.getSystemService(AudioManager::class.java)
    private val component = ComponentName(ctx, NotifListener::class.java)
    private val handler = Handler(Looper.getMainLooper())
    private var controller: MediaController? = null
    private var lastSent = ""
    private var started = false

    private val sessionsListener = MediaSessionManager.OnActiveSessionsChangedListener { list ->
        pick(list ?: emptyList())
    }

    private val callback = object : MediaController.Callback() {
        override fun onMetadataChanged(metadata: MediaMetadata?) {
            schedulePublish()
            scheduleRecord()
        }
        override fun onPlaybackStateChanged(state: PlaybackState?) {
            // Eine andere App könnte gerade angefangen haben zu spielen
            refreshSessions()
            schedulePublish()
            scheduleRecord()
        }
        override fun onSessionDestroyed() = refreshSessions()
    }

    /** Startet, sobald der Benachrichtigungszugriff erteilt ist (wird wiederholt aufgerufen). */
    fun start() {
        if (started) return
        try {
            msm.addOnActiveSessionsChangedListener(sessionsListener, component, handler)
            started = true
            refreshSessions()
        } catch (_: SecurityException) {
            // Noch kein Benachrichtigungszugriff
        }
    }

    fun stop() {
        if (started) msm.removeOnActiveSessionsChangedListener(sessionsListener)
        controller?.unregisterCallback(callback)
        controller = null
        started = false
    }

    private fun refreshSessions() {
        try {
            pick(msm.getActiveSessions(component))
        } catch (_: SecurityException) {
        }
    }

    private fun pick(list: List<MediaController>) {
        val best = list.firstOrNull { it.playbackState?.state == PlaybackState.STATE_PLAYING }
            ?: list.firstOrNull { it.packageName == SPOTIFY }
            ?: list.firstOrNull()
        if (best?.sessionToken != controller?.sessionToken) {
            controller?.unregisterCallback(callback)
            controller = best
            best?.registerCallback(callback, handler)
        }
        schedulePublish()
    }

    private val publishRunnable = Runnable { publish(force = false) }
    private fun schedulePublish() {
        handler.removeCallbacks(publishRunnable)
        handler.postDelayed(publishRunnable, 250)  // Metadaten + Status kommen oft kurz nacheinander
    }

    /** Kurzbeschreibung für den KI-Assistenten, null = keine Medien-Session. */
    fun describe(): String? {
        val c = controller ?: return null
        val meta = c.metadata ?: return null
        val title = meta.getString(MediaMetadata.METADATA_KEY_TITLE) ?: return null
        val artist = meta.getString(MediaMetadata.METADATA_KEY_ARTIST) ?: ""
        val playing = c.playbackState?.state == PlaybackState.STATE_PLAYING
        return "\"$title\"" + (if (artist.isNotBlank()) " von $artist" else "") +
            " in ${appLabel(c.packageName)} (" + (if (playing) "spielt" else "pausiert") + ")"
    }

    fun publish(force: Boolean) {
        val c = controller
        val meta = c?.metadata
        if (c == null || meta == null) {
            if (force || lastSent != "OFF") bridge.send("MEDIA_OFF")
            lastSent = "OFF"
            return
        }
        val state = c.playbackState
        val playing = state?.state == PlaybackState.STATE_PLAYING
        val title = meta.getString(MediaMetadata.METADATA_KEY_TITLE) ?: ""
        val artist = meta.getString(MediaMetadata.METADATA_KEY_ARTIST)
            ?: meta.getString(MediaMetadata.METADATA_KEY_ALBUM_ARTIST) ?: ""
        val dur = (meta.getLong(MediaMetadata.METADATA_KEY_DURATION) / 1000).coerceAtLeast(0)
        val pos = positionSec(state)
        val appName = appLabel(c.packageName)

        // Position zählt die Uhr selbst hoch – nur bei Sprüngen (Spulen) neu senden
        val key = "$title|$artist|$playing|$dur|${pos / 5}"
        if (!force && key == lastSent) return
        lastSent = key
        fun clean(s: String) = s.replace('|', '/').replace('\n', ' ')
        bridge.send("MEDIA", clean(title), clean(artist), if (playing) "1" else "0", pos.toString(), dur.toString(), clean(appName))
    }

    private fun positionSec(state: PlaybackState?): Long {
        if (state == null) return 0
        var pos = state.position
        if (state.state == PlaybackState.STATE_PLAYING && state.lastPositionUpdateTime > 0) {
            pos += ((SystemClock.elapsedRealtime() - state.lastPositionUpdateTime) * state.playbackSpeed).toLong()
        }
        return (pos / 1000).coerceAtLeast(0)
    }

    private fun appLabel(pkg: String): String = try {
        val pm = ctx.packageManager
        pm.getApplicationLabel(pm.getApplicationInfo(pkg, 0)).toString()
    } catch (_: Exception) {
        pkg
    }

    /** Befehle der Uhr: toggle, play, pause, next, prev, vol_up, vol_down */
    fun command(cmd: String) {
        val t = controller?.transportControls
        when (cmd.trim()) {
            "toggle" -> if (controller?.playbackState?.state == PlaybackState.STATE_PLAYING) t?.pause() else t?.play()
            "play" -> t?.play()
            "pause" -> t?.pause()
            "next" -> t?.skipToNext()
            "prev" -> t?.skipToPrevious()
            "vol_up" -> audio.adjustStreamVolume(AudioManager.STREAM_MUSIC, AudioManager.ADJUST_RAISE, 0)
            "vol_down" -> audio.adjustStreamVolume(AudioManager.STREAM_MUSIC, AudioManager.ADJUST_LOWER, 0)
        }
        schedulePublish()
    }

    // --- Hörverlauf (für "spiel Musik, die ich mag") ------------------------------------------

    private val hist = ctx.getSharedPreferences("listening", Context.MODE_PRIVATE)
    private var lastRecorded = ""

    private class Play(val title: String, val artist: String, var count: Int, var last: Long)

    private fun loadHistory(): MutableList<Play> = try {
        val arr = JSONArray(hist.getString("plays", "[]"))
        MutableList(arr.length()) { i ->
            val o = arr.getJSONObject(i)
            Play(o.getString("t"), o.getString("a"), o.getInt("n"), o.getLong("l"))
        }
    } catch (_: Exception) {
        mutableListOf()
    }

    private val recordRunnable = Runnable { record() }
    private fun scheduleRecord() {
        handler.removeCallbacks(recordRunnable)
        handler.postDelayed(recordRunnable, 20_000)  // erst zählen, wenn der Titel 20 s lang läuft
    }

    /** Merkt sich Titel, die wirklich gehört werden (nur Musik-Apps, keine Videos). */
    private fun record() {
        val c = controller ?: return
        if (c.playbackState?.state != PlaybackState.STATE_PLAYING) return
        if (c.packageName !in MUSIC_APPS) return
        val meta = c.metadata ?: return
        val title = meta.getString(MediaMetadata.METADATA_KEY_TITLE)?.takeIf { it.isNotBlank() } ?: return
        val artist = (meta.getString(MediaMetadata.METADATA_KEY_ARTIST)
            ?: meta.getString(MediaMetadata.METADATA_KEY_ALBUM_ARTIST) ?: "").trim()
        val key = "$title|$artist"
        if (key == lastRecorded) return
        lastRecorded = key

        val list = loadHistory()
        val now = System.currentTimeMillis()
        val hit = list.firstOrNull { it.title == title && it.artist == artist }
        if (hit != null) { hit.count++; hit.last = now } else list += Play(title, artist, 1, now)
        // Die 400 wichtigsten behalten (oft + kürzlich gehört)
        val keep = list.sortedByDescending { it.count * 3 + (it.last / 86_400_000L) }.take(400)
        val arr = JSONArray()
        keep.forEach { arr.put(JSONObject().put("t", it.title).put("a", it.artist).put("n", it.count).put("l", it.last)) }
        hist.edit().putString("plays", arr.toString()).apply()
    }

    /** Zusammenfassung des Hörverlaufs für die KI. */
    fun historySummary(): String {
        val list = loadHistory()
        if (list.isEmpty()) return "Noch kein Hörverlauf gespeichert (WatchBridge merkt sich ab jetzt, was in Spotify läuft). " +
            "Wähle nach dem aktuell laufenden Titel oder frag nach einem Künstler."
        val artists = list.filter { it.artist.isNotBlank() }.groupBy { it.artist }
            .map { (a, l) -> a to l.sumOf { it.count } }.sortedByDescending { it.second }.take(40)
        val top = list.sortedByDescending { it.count }.take(40)
        val recent = list.sortedByDescending { it.last }.take(25)
        fun line(p: Play) = "${p.title} – ${p.artist}"
        return "Lieblingskünstler (Anzahl Wiedergaben): " + artists.joinToString(", ") { "${it.first} (${it.second})" } +
            "\nMeistgehörte Titel: " + top.joinToString("; ") { line(it) + " (${it.count}x)" } +
            "\nZuletzt gehört: " + recent.joinToString("; ") { line(it) }
    }

    // --- Musik per Suche starten --------------------------------------------------------------

    /**
     * Startet in Spotify die beste Übereinstimmung für [query] (wie "Hey Google, spiel …").
     * [kind]: track, artist, album, playlist oder any. Rückgabe: Fehlertext oder null.
     */
    fun playFromSearch(query: String, kind: String, artist: String?): String? {
        val extras = Bundle().apply {
            putString(QUERY, query)
            when (kind) {
                "artist" -> {
                    putString(MediaStore.EXTRA_MEDIA_FOCUS, MediaStore.Audio.Artists.ENTRY_CONTENT_TYPE)
                    putString(MediaStore.EXTRA_MEDIA_ARTIST, artist ?: query)
                }
                "track" -> {
                    putString(MediaStore.EXTRA_MEDIA_FOCUS, "vnd.android.cursor.item/audio")
                    putString(MediaStore.EXTRA_MEDIA_TITLE, query)
                    artist?.let { putString(MediaStore.EXTRA_MEDIA_ARTIST, it) }
                }
                "album" -> {
                    putString(MediaStore.EXTRA_MEDIA_FOCUS, MediaStore.Audio.Albums.ENTRY_CONTENT_TYPE)
                    putString(MediaStore.EXTRA_MEDIA_ALBUM, query)
                    artist?.let { putString(MediaStore.EXTRA_MEDIA_ARTIST, it) }
                }
                "playlist" -> {
                    putString(MediaStore.EXTRA_MEDIA_FOCUS, "vnd.android.cursor.item/playlist")
                    putString("android.intent.extra.playlist", query)
                }
            }
        }
        val searchText = if (artist != null && kind != "artist") "$query $artist" else query

        // 1. Läuft Spotify schon (auch pausiert)? -> direkt über die Medien-Session, ohne die App zu öffnen
        val session = try {
            msm.getActiveSessions(component).firstOrNull { it.packageName == SPOTIFY }
        } catch (_: SecurityException) {
            return "Kein Mitteilungszugriff für WatchBridge"
        }
        if (session != null) {
            session.transportControls.playFromSearch(searchText, extras)
            return null
        }
        // 2. Sonst Spotify per Intent starten (Android erlaubt das aus dem Hintergrund nicht immer)
        return try {
            ctx.startActivity(Intent(MediaStore.INTENT_ACTION_MEDIA_PLAY_FROM_SEARCH).apply {
                setPackage(SPOTIFY)
                putExtras(extras)
                putExtra(QUERY, searchText)
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            })
            null
        } catch (_: Exception) {
            "Spotify läuft nicht und konnte nicht gestartet werden – bitte Spotify einmal öffnen"
        }
    }

    /** Was in Spotify gerade läuft (Titel – Künstler), für die Bestätigung nach playFromSearch. */
    fun nowPlaying(): String? {
        val c = try {
            msm.getActiveSessions(component).firstOrNull { it.packageName == SPOTIFY }
        } catch (_: SecurityException) { null } ?: return null
        val meta = c.metadata ?: return null
        val t = meta.getString(MediaMetadata.METADATA_KEY_TITLE) ?: return null
        val a = meta.getString(MediaMetadata.METADATA_KEY_ARTIST) ?: ""
        val playing = c.playbackState?.state == PlaybackState.STATE_PLAYING
        return "$t – $a" + if (playing) "" else " (pausiert)"
    }

    companion object {
        const val SPOTIFY = "com.spotify.music"
        private const val QUERY = "query"   // = SearchManager.QUERY
        private val MUSIC_APPS = setOf(SPOTIFY, "com.google.android.apps.youtube.music", "deezer.android.app",
            "com.amazon.mp3", "com.apple.android.music", "com.soundcloud.android")
    }
}
