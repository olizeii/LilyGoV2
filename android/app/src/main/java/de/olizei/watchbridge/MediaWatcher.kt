package de.olizei.watchbridge

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.media.AudioManager
import android.media.MediaMetadata
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.media.session.PlaybackState
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import kotlinx.coroutines.launch

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
        override fun onMetadataChanged(metadata: MediaMetadata?) = schedulePublish()
        override fun onPlaybackStateChanged(state: PlaybackState?) {
            // Eine andere App könnte gerade angefangen haben zu spielen
            refreshSessions()
            schedulePublish()
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

    private var sessions: List<MediaController> = emptyList()
    private var pinned: String? = null   // auf der Uhr gewählte Quelle (bis eine andere App losspielt)
    private var lastSources = ""

    /** Sessions, die als Quelle taugen (keine Telefon-/Bluetooth-Dienste). */
    private fun usable(list: List<MediaController>) = list.filter { it.packageName !in IGNORED }

    private fun pick(list: List<MediaController>) {
        sessions = usable(list)
        val playing = sessions.firstOrNull { it.playbackState?.state == PlaybackState.STATE_PLAYING }
        if (playing != null && playing.packageName != pinned) pinned = null
        val best = playing
            ?: sessions.firstOrNull { it.packageName == pinned }
            ?: sessions.firstOrNull { it.packageName == SPOTIFY && it.metadata != null }
            ?: sessions.firstOrNull { it.metadata != null }
            ?: sessions.firstOrNull()
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

    /** Liste der Quellen für die Uhr: MEDIA_SRC|<aktuell>|<App1>|<App2>… (Spotify immer, wenn verbunden). */
    private fun publishSources(force: Boolean) {
        val labels = LinkedHashSet<String>()
        controller?.let { labels += appLabel(it.packageName) }
        sessions.forEach { labels += appLabel(it.packageName) }
        if (WatchBridgeApp.instance.spotify.connected || isInstalled(SPOTIFY)) labels += appLabel(SPOTIFY)
        val cur = controller?.let { appLabel(it.packageName) } ?: ""
        val list = labels.take(3)
        val key = cur + "|" + list.joinToString("|")
        if (!force && key == lastSources) return
        lastSources = key
        bridge.send("MEDIA_SRC", *(listOf(cur) + list).map { it.replace('|', '/') }.toTypedArray())
    }

    private fun isInstalled(pkg: String) = try {
        ctx.packageManager.getApplicationInfo(pkg, 0); true
    } catch (_: Exception) { false }

    /** Uhr hat eine Quelle angetippt: auf diese App umschalten und abspielen. */
    private fun switchTo(label: String) {
        val target = sessions.firstOrNull { appLabel(it.packageName).equals(label, true) }
        if (target != null) {
            pinned = target.packageName
            if (target.sessionToken != controller?.sessionToken) {
                controller?.unregisterCallback(callback)
                controller = target
                target.registerCallback(callback, handler)
            }
            target.transportControls.play()
            publish(force = true)
            return
        }
        // Spotify ohne Session (beendet) -> über die Web API auf dem Handy fortsetzen
        if (label.equals(appLabel(SPOTIFY), true)) {
            pinned = SPOTIFY
            if (!wakeSpotify() && WatchBridgeApp.instance.spotify.connected) {
                WatchBridgeApp.instance.scope.launch(kotlinx.coroutines.Dispatchers.IO) {
                    try { WatchBridgeApp.instance.spotify.resume() } catch (e: Exception) {
                        android.util.Log.w("MediaWatcher", "Spotify fortsetzen", e)
                    }
                }
            }
        }
    }

    fun publish(force: Boolean) {
        publishSources(force)
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
        if (cmd.startsWith("src|")) {
            switchTo(cmd.substringAfter('|').trim())
            schedulePublish()
            return
        }
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

    /** Weckt Spotify über seine Medien-Session (geht auch aus dem Hintergrund). false = keine Session. */
    fun wakeSpotify(): Boolean {
        val s = try {
            msm.getActiveSessions(component).firstOrNull { it.packageName == SPOTIFY }
        } catch (_: SecurityException) { null }
        if (s != null) {
            // Wie die Play-Taste am Kopfhörer: Spotify setzt die letzte Wiedergabe fort und meldet sich bei Connect an
            s.dispatchMediaButtonEvent(android.view.KeyEvent(android.view.KeyEvent.ACTION_DOWN, android.view.KeyEvent.KEYCODE_MEDIA_PLAY))
            s.dispatchMediaButtonEvent(android.view.KeyEvent(android.view.KeyEvent.ACTION_UP, android.view.KeyEvent.KEYCODE_MEDIA_PLAY))
            s.transportControls.play()
            return true
        }
        // Keine Session (Spotify beendet): Play-Taste direkt an Spotifys Empfänger schicken
        return try {
            for (action in intArrayOf(android.view.KeyEvent.ACTION_DOWN, android.view.KeyEvent.ACTION_UP)) {
                ctx.sendBroadcast(Intent(Intent.ACTION_MEDIA_BUTTON).setComponent(
                    ComponentName(SPOTIFY, "com.spotify.mediasession.mediasession.receiver.MediaButtonReceiver"),
                ).putExtra(Intent.EXTRA_KEY_EVENT, android.view.KeyEvent(action, android.view.KeyEvent.KEYCODE_MEDIA_PLAY)))
            }
            true
        } catch (_: Exception) {
            false
        }
    }

    companion object {
        const val SPOTIFY = "com.spotify.music"
        private val IGNORED = setOf("com.android.server.telecom", "com.android.bluetooth", "android")
    }
}
