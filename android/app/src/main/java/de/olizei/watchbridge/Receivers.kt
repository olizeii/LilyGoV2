package de.olizei.watchbridge

import android.Manifest
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import androidx.core.content.ContextCompat

/** Nach Neustart / App-Update automatisch wieder verbinden. */
class BootReceiver : BroadcastReceiver() {
    override fun onReceive(ctx: Context, intent: Intent) {
        val app = WatchBridgeApp.instance
        if (!app.prefs.autoStart) return
        val ok = ContextCompat.checkSelfPermission(ctx, Manifest.permission.BLUETOOTH_CONNECT) ==
            PackageManager.PERMISSION_GRANTED
        if (ok) BridgeService.start(ctx)
    }
}

/** System-Scan (PendingIntent) hat die Uhr gesehen -> sofort direkt verbinden. */
class WatchSeenReceiver : BroadcastReceiver() {
    override fun onReceive(ctx: Context, intent: Intent) {
        val results: List<android.bluetooth.le.ScanResult> = if (android.os.Build.VERSION.SDK_INT >= 33) {
            intent.getParcelableArrayListExtra(android.bluetooth.le.BluetoothLeScanner.EXTRA_LIST_SCAN_RESULT,
                android.bluetooth.le.ScanResult::class.java)
        } else {
            @Suppress("DEPRECATION")
            intent.getParcelableArrayListExtra(android.bluetooth.le.BluetoothLeScanner.EXTRA_LIST_SCAN_RESULT)
        } ?: return
        val dev = results.firstOrNull()?.device ?: return
        val app = WatchBridgeApp.instance
        if (!app.prefs.autoStart) return
        if (!BridgeService.running) {
            // Dienst wurde beendet -> neu starten (Android kann das aus dem Hintergrund ablehnen)
            try { BridgeService.start(ctx) } catch (_: Exception) { return }
        }
        app.link.onWatchSeen(dev)
    }
}

/**
 * Schnittstelle für Tasker / MacroDroid:
 *   Aktion "Intent senden", Action = de.olizei.watchbridge.SEND,
 *   Extra msg = "TOPIC|payload" (z.B. "SHORTCUT|1|Licht an"), Ziel = Broadcast.
 */
class TaskerReceiver : BroadcastReceiver() {
    override fun onReceive(ctx: Context, intent: Intent) {
        val msg = intent.getStringExtra("msg") ?: return
        if (msg.isBlank()) return
        WatchBridgeApp.instance.bridge.sendRaw(msg)
    }
}
