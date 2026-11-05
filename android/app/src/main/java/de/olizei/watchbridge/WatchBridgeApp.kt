package de.olizei.watchbridge

import android.app.Application
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob

/**
 * Hält die app-weiten Singletons. Der [BridgeService] startet die Verbindung,
 * die Activity zeigt nur Zustände an und ändert Einstellungen.
 */
class WatchBridgeApp : Application() {

    val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    lateinit var prefs: Prefs private set
    lateinit var notes: NotesRepo private set
    lateinit var link: BleLink private set
    lateinit var bridge: Bridge private set
    lateinit var ai: AiAssistant private set
    lateinit var spotify: SpotifyApi private set
    lateinit var beats: BeatUploader private set
    lateinit var songs: SongRecognizer private set

    override fun onCreate() {
        super.onCreate()
        instance = this
        prefs = Prefs(this)
        notes = NotesRepo(this)
        link = BleLink(this, scope, prefs)
        bridge = Bridge(this)
        spotify = SpotifyApi(this, prefs)
        ai = AiAssistant(this)
        beats = BeatUploader(this)
        songs = SongRecognizer(this)
    }

    companion object {
        lateinit var instance: WatchBridgeApp private set
    }
}
