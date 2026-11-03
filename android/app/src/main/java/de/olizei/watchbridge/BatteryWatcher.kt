package de.olizei.watchbridge

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.BatteryManager
import android.os.Handler
import android.os.Looper
import androidx.core.content.ContextCompat
import androidx.core.content.IntentCompat
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/**
 * Akkustände: Handy (ACTION_BATTERY_CHANGED) und Ray-Ban Meta (Bluetooth-Akkustand,
 * den Android von Headsets/Brillen über HFP meldet).
 */
@SuppressLint("MissingPermission")
class BatteryWatcher(private val ctx: Context, private val bridge: Bridge, private val prefs: Prefs) {

    private var phonePct = -1
    private var phoneCharging = false
    private val _raybanPct = MutableStateFlow(-1)
    val raybanPct: StateFlow<Int> = _raybanPct
    private val handler = Handler(Looper.getMainLooper())

    private val phoneReceiver = object : BroadcastReceiver() {
        override fun onReceive(c: Context, i: Intent) {
            val level = i.getIntExtra(BatteryManager.EXTRA_LEVEL, -1)
            val scale = i.getIntExtra(BatteryManager.EXTRA_SCALE, 100)
            val pct = if (level >= 0 && scale > 0) level * 100 / scale else -1
            val charging = i.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0) != 0
            if (pct != phonePct || charging != phoneCharging) {
                phonePct = pct
                phoneCharging = charging
                sendPhone()
            }
        }
    }

    private val btReceiver = object : BroadcastReceiver() {
        override fun onReceive(c: Context, i: Intent) {
            val dev = IntentCompat.getParcelableExtra(i, BluetoothDevice.EXTRA_DEVICE, BluetoothDevice::class.java) ?: return
            if (!isRayban(dev)) return
            when (i.action) {
                ACTION_BATTERY_LEVEL_CHANGED -> setRayban(i.getIntExtra(EXTRA_BATTERY_LEVEL, -1))
                BluetoothDevice.ACTION_ACL_CONNECTED -> handler.postDelayed({ pollRayban() }, 4000)
                BluetoothDevice.ACTION_ACL_DISCONNECTED -> setRayban(-1)
            }
        }
    }

    private val poll = object : Runnable {
        override fun run() {
            pollRayban()
            handler.postDelayed(this, 10 * 60_000L)
        }
    }

    fun start() {
        ctx.registerReceiver(phoneReceiver, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
        val f = IntentFilter().apply {
            addAction(ACTION_BATTERY_LEVEL_CHANGED)
            addAction(BluetoothDevice.ACTION_ACL_CONNECTED)
            addAction(BluetoothDevice.ACTION_ACL_DISCONNECTED)
        }
        ContextCompat.registerReceiver(ctx, btReceiver, f, ContextCompat.RECEIVER_EXPORTED)  // System-Broadcasts
        handler.post(poll)
    }

    fun stop() {
        handler.removeCallbacks(poll)
        try {
            ctx.unregisterReceiver(phoneReceiver)
            ctx.unregisterReceiver(btReceiver)
        } catch (_: Exception) {
        }
    }

    fun sendAll() {
        sendPhone()
        bridge.send("BAT", "rayban", _raybanPct.value.toString())
    }

    private fun sendPhone() = bridge.send("BAT", "phone", phonePct.toString(), if (phoneCharging) "1" else "0")

    private fun setRayban(pct: Int) {
        if (pct == _raybanPct.value) return
        _raybanPct.value = pct
        bridge.send("BAT", "rayban", pct.toString())
    }

    /** Ausgewähltes Gerät – oder automatisch erkannt am Namen. */
    private fun isRayban(dev: BluetoothDevice): Boolean {
        val chosen = prefs.raybanAddress
        if (chosen != null) return dev.address == chosen
        return looksLikeRayban(dev.name)
    }

    fun pollRayban() {
        val adapter = ctx.getSystemService(BluetoothManager::class.java)?.adapter ?: return
        val dev = try {
            adapter.bondedDevices.firstOrNull { isRayban(it) }
        } catch (_: SecurityException) {
            null
        } ?: return
        // BluetoothDevice.getBatteryLevel() ist versteckt, aber auf allen gängigen Android-Versionen erreichbar
        val level = try {
            dev.javaClass.getMethod("getBatteryLevel").invoke(dev) as Int
        } catch (_: Exception) {
            -1
        }
        setRayban(if (level in 0..100) level else -1)
    }

    companion object {
        const val ACTION_BATTERY_LEVEL_CHANGED = "android.bluetooth.device.action.BATTERY_LEVEL_CHANGED"
        const val EXTRA_BATTERY_LEVEL = "android.bluetooth.device.extra.BATTERY_LEVEL"

        fun looksLikeRayban(name: String?): Boolean {
            val n = name?.lowercase() ?: return false
            return n.contains("ray-ban") || n.contains("rayban") || n.contains("meta")
        }
    }
}
