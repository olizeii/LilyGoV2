package de.olizei.watchbridge

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.Manifest
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.os.IBinder
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch

/**
 * Hält die Verbindung zur Uhr dauerhaft offen (Vordergrund-Dienst mit
 * fester Benachrichtigung) und startet alle Datenquellen.
 */
class BridgeService : Service() {

    private val app get() = WatchBridgeApp.instance
    private var stateJob: Job? = null

    // Uhrzeit nachführen, wenn sie am Handy geändert wird
    private val timeReceiver = object : BroadcastReceiver() {
        override fun onReceive(c: Context, i: Intent) = app.bridge.sendTime()
    }

    override fun onCreate() {
        super.onCreate()
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(
            NotificationChannel(CHANNEL, "Verbindung zur Uhr", NotificationManager.IMPORTANCE_MIN).apply {
                setShowBadge(false)
            },
        )
        goForeground()

        val bridge = app.bridge
        bridge.battery = BatteryWatcher(this, bridge, app.prefs).also { it.start() }
        bridge.media = MediaWatcher(this, bridge).also { it.start() }
        bridge.untis = UntisSync(app.scope, app.prefs, bridge).also { it.start() }
        bridge.maps = MapProvider(this, app.scope, bridge).also { it.start() }
        app.link.start()

        ContextCompat.registerReceiver(
            this, timeReceiver,
            IntentFilter().apply {
                addAction(Intent.ACTION_TIME_CHANGED)
                addAction(Intent.ACTION_TIMEZONE_CHANGED)
            },
            ContextCompat.RECEIVER_NOT_EXPORTED,
        )

        stateJob = app.scope.launch {
            app.link.state.collectLatest { s ->
                val text = when (s) {
                    BleLink.State.CONNECTED -> "Mit Uhr verbunden"
                    BleLink.State.SCANNING -> "Suche Uhr …"
                    BleLink.State.CONNECTING -> "Verbinde …"
                    BleLink.State.OFF -> "Bluetooth aus"
                }
                lastText = text
                nm.notify(NOTIF_ID, buildNotification(text))
            }
        }
        running = true
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        // Start aus der sichtbaren App -> Android gewährt jetzt den Mikrofon-Zugriff auch für den Hintergrund
        android.util.Log.d("BridgeService", "start fromUi=${intent?.getBooleanExtra(EXTRA_FROM_UI, false)}")
        if (intent?.getBooleanExtra(EXTRA_FROM_UI, false) == true) {
            goForeground()
            micReady = true
        }
        return START_STICKY
    }

    /**
     * Android erlaubt Mikrofon-Zugriff im Hintergrund nur einem Vordergrund-Dienst vom Typ "microphone",
     * und der darf nur gestartet werden, während die App sichtbar ist. Beim Autostart (Boot) klappt
     * das nicht – dann erst nur "connectedDevice", und beim nächsten Öffnen der App wird nachgerüstet.
     */
    private var lastText = "Verbinde …"

    private fun goForeground() {
        val notif = buildNotification(lastText)
        val micOk = ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED
        if (micOk) {
            try {
                ServiceCompat.startForeground(this, NOTIF_ID, notif,
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE or ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE)
                return
            } catch (e: Exception) {
                // App gerade nicht im Vordergrund
                android.util.Log.w("BridgeService", "Mikrofon-Typ abgelehnt", e)
            }
        }
        ServiceCompat.startForeground(this, NOTIF_ID, notif, ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
    }

    override fun onDestroy() {
        running = false
        stateJob?.cancel()
        try {
            unregisterReceiver(timeReceiver)
        } catch (_: Exception) {
        }
        val bridge = app.bridge
        app.link.stop()
        bridge.battery?.stop(); bridge.battery = null
        bridge.media?.stop(); bridge.media = null
        bridge.untis?.stop(); bridge.untis = null
        bridge.maps?.shutdown(); bridge.maps = null
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun buildNotification(text: String): Notification {
        val open = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        return Notification.Builder(this, CHANNEL)
            .setSmallIcon(R.drawable.ic_stat_watch)
            .setContentTitle("WatchBridge")
            .setContentText(text)
            .setContentIntent(open)
            .setOngoing(true)
            .setCategory(Notification.CATEGORY_SERVICE)
            .build()
    }

    companion object {
        private const val CHANNEL = "link"
        private const val NOTIF_ID = 1
        @Volatile var running = false
        /** true = Mikrofon (Spracherkennung) funktioniert auch, wenn die App geschlossen ist. */
        @Volatile var micReady = false

        private const val EXTRA_FROM_UI = "from_ui"

        /** [fromUi] = aus der sichtbaren App gestartet (nur dann gibt Android das Mikrofon frei). */
        fun start(ctx: Context, fromUi: Boolean = false) {
            ContextCompat.startForegroundService(ctx,
                Intent(ctx, BridgeService::class.java).putExtra(EXTRA_FROM_UI, fromUi))
        }

        fun stop(ctx: Context) {
            ctx.stopService(Intent(ctx, BridgeService::class.java))
        }
    }
}
