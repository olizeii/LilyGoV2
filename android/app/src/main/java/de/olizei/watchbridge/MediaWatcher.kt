package de.olizei.watchbridge

import android.content.ComponentName
import android.content.Context
import android.media.AudioManager
import android.media.MediaMetadata
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.media.session.PlaybackState
import android.os.Handler
import android.os.Looper
import android.os.SystemClock

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

    companion object {
        const val SPOTIFY = "com.spotify.music"
    }
}
