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
        ServiceCompat.startForeground(
            this, NOTIF_ID, buildNotification("Verbinde …"),
            ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE,
        )

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
                nm.notify(NOTIF_ID, buildNotification(text))
            }
        }
        running = true
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int = START_STICKY

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

        fun start(ctx: Context) {
            ContextCompat.startForegroundService(ctx, Intent(ctx, BridgeService::class.java))
        }

        fun stop(ctx: Context) {
            ctx.stopService(Intent(ctx, BridgeService::class.java))
        }
    }
}
