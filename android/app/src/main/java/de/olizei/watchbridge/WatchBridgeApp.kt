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

    override fun onCreate() {
        super.onCreate()
        instance = this
        prefs = Prefs(this)
        notes = NotesRepo(this)
        link = BleLink(this, scope, prefs)
        bridge = Bridge(this)
        ai = AiAssistant(this)
    }

    companion object {
        lateinit var instance: WatchBridgeApp private set
    }
}
