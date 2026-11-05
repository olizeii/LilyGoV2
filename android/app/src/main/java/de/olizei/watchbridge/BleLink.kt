package de.olizei.watchbridge

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import android.util.Log
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull
import java.util.UUID

/**
 * BLE-Verbindung zur Uhr (Nordic UART Service).
 *
 *  - Sucht die Uhr einmal per Scan, merkt sich die Adresse und verbindet danach
 *    direkt (autoConnect -> Android verbindet von selbst neu, sobald sie in Reichweite ist).
 *  - Nachrichten > MTU werden gestückelt: jedes Teilstück außer dem letzten endet auf '~'.
 *  - Writes laufen strikt nacheinander (mit Antwort), damit nichts verloren geht.
 */
@SuppressLint("MissingPermission")  // Berechtigungen prüft die Activity vor dem Dienststart
class BleLink(
    private val ctx: Context,
    private val scope: CoroutineScope,
    private val prefs: Prefs,
) {
    enum class State { OFF, SCANNING, CONNECTING, CONNECTED }

    private val _state = MutableStateFlow(State.OFF)
    val state: StateFlow<State> = _state

    /** Wird im Binder-Thread aufgerufen. */
    var onMessage: ((String) -> Unit)? = null

    private val adapter get() = ctx.getSystemService(BluetoothManager::class.java)?.adapter
    private val handler = Handler(Looper.getMainLooper())
    private var gatt: BluetoothGatt? = null
    private var rx: BluetoothGattCharacteristic? = null
    private var mtu = 23
    private var running = false
    private var scanning = false
    private var failures = 0

    private val outbox = Channel<ByteArray>(Channel.UNLIMITED)
    @Volatile private var writeResult: CompletableDeferred<Boolean>? = null
    private var writer: Job? = null

    fun start() {
        if (running) return
        running = true
        writer = scope.launch { writerLoop() }
        connectOrScan()
    }

    fun stop() {
        running = false
        stopScan()
        setBackgroundScan(false)
        gatt?.disconnect()
        gatt?.close()
        gatt = null
        rx = null
        writer?.cancel()
        _state.value = State.OFF
    }

    /** Gemerkte Uhr vergessen und neu suchen (z.B. nach Firmware-Wechsel). */
    fun forgetAndRescan() {
        prefs.watchAddress = null
        gatt?.disconnect()
        gatt?.close()
        gatt = null
        rx = null
        if (running) scan()
    }

    // --- Senden -----------------------------------------------------------------

    /** Ganze Nachricht "TOPIC|payload" einreihen. Atomar, damit Teilstücke nicht gemischt werden. */
    @Synchronized
    fun send(message: String) {
        if (_state.value != State.CONNECTED) return
        var bytes = message.toByteArray(Charsets.UTF_8)
        if (bytes.size > MAX_MESSAGE) bytes = bytes.copyOf(MAX_MESSAGE)
        // Endet die Nachricht selbst auf '~', hielte die Uhr sie für unvollständig
        if (bytes.isNotEmpty() && bytes.last() == '~'.code.toByte()) bytes += ' '.code.toByte()

        val payload = (mtu - 3).coerceAtLeast(20)
        if (bytes.size <= payload) {
            outbox.trySend(bytes)
            return
        }
        val step = payload - 1  // Platz für das '~'
        var i = 0
        while (i < bytes.size) {
            val end = minOf(i + step, bytes.size)
            val last = end == bytes.size
            val chunk = bytes.copyOfRange(i, end)
            outbox.trySend(if (last) chunk else chunk + '~'.code.toByte())
            i = end
        }
    }

    private suspend fun writerLoop() {
        for (bytes in outbox) {
            if (_state.value != State.CONNECTED) continue  // veraltet – nach SYNC kommt alles neu
            var ok = writeOnce(bytes)
            if (!ok && _state.value == State.CONNECTED) ok = writeOnce(bytes)
            if (!ok) Log.w(TAG, "Write fehlgeschlagen (${bytes.size} B)")
        }
    }

    private suspend fun writeOnce(bytes: ByteArray): Boolean {
        val g = gatt ?: return false
        val c = rx ?: return false
        val done = CompletableDeferred<Boolean>()
        writeResult = done
        val started = if (Build.VERSION.SDK_INT >= 33) {
            g.writeCharacteristic(c, bytes, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == BluetoothGatt.GATT_SUCCESS
        } else {
            @Suppress("DEPRECATION")
            c.value = bytes
            c.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            @Suppress("DEPRECATION")
            g.writeCharacteristic(c)
        }
        if (!started) return false
        return withTimeoutOrNull(3000) { done.await() } ?: false
    }

    // --- Verbinden ----------------------------------------------------------------

    // --- "Hallo" der Uhr im Hintergrund hören --------------------------------------------------

    private var bgScan = false
    private var lastSeenConnect = 0L

    /**
     * Stromsparender System-Scan nach der Uhr, solange sie getrennt ist. Android liefert Treffer per
     * PendingIntent an [WatchSeenReceiver] – auch wenn WatchBridge gerade schläft oder beendet wurde.
     */
    private fun setBackgroundScan(on: Boolean) {
        if (on == bgScan) return
        val scanner = adapter?.bluetoothLeScanner ?: return
        try {
            if (on) {
                val filters = listOf(ScanFilter.Builder().setServiceUuid(ParcelUuid(SERVICE)).build())
                val settings = ScanSettings.Builder()
                    .setScanMode(ScanSettings.SCAN_MODE_LOW_POWER)
                    .setCallbackType(ScanSettings.CALLBACK_TYPE_ALL_MATCHES)
                    .build()
                scanner.startScan(filters, settings, seenIntent())
            } else {
                scanner.stopScan(seenIntent())
            }
            bgScan = on
        } catch (e: Exception) {
            Log.w(TAG, "Hintergrund-Scan", e)
        }
    }

    private fun seenIntent(): PendingIntent = PendingIntent.getBroadcast(
        ctx, 7, Intent(ctx, WatchSeenReceiver::class.java),
        PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_MUTABLE,
    )

    /** Die Uhr advertised gerade (getrennt) -> direkt verbinden statt auf autoConnect zu warten. */
    fun onWatchSeen(device: BluetoothDevice) {
        handler.post {
            if (!running || _state.value == State.CONNECTED) return@post
            val now = System.currentTimeMillis()
            if (now - lastSeenConnect < 8_000) return@post   // Verbindungsaufbau läuft schon
            lastSeenConnect = now
            if (prefs.watchAddress == null) prefs.watchAddress = device.address
            if (device.address != prefs.watchAddress) return@post
            Log.d(TAG, "Uhr gesehen -> direkt verbinden")
            stopScan()
            connect(device, autoConnect = false)
        }
    }

    private fun connectOrScan() {
        if (!running) return
        val addr = prefs.watchAddress
        val a = adapter
        if (a == null || !a.isEnabled) {
            _state.value = State.OFF
            handler.postDelayed({ connectOrScan() }, 10_000)  // Bluetooth evtl. später an
            return
        }
        setBackgroundScan(true)   // falls die Uhr gleich "Hallo" sagt
        if (addr != null) connect(a.getRemoteDevice(addr), autoConnect = failures > 0) else scan()
    }

    private fun scan() {
        val scanner = adapter?.bluetoothLeScanner ?: return
        if (scanning) return
        scanning = true
        _state.value = State.SCANNING
        val filters = listOf(ScanFilter.Builder().setServiceUuid(ParcelUuid(SERVICE)).build())
        val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        scanner.startScan(filters, settings, scanCallback)
        handler.postDelayed({  // Scan nicht ewig laufen lassen (Akku), später erneut
            if (scanning) {
                stopScan()
                handler.postDelayed({ connectOrScan() }, 30_000)
            }
        }, 30_000)
    }

    private fun stopScan() {
        if (!scanning) return
        scanning = false
        try {
            adapter?.bluetoothLeScanner?.stopScan(scanCallback)
        } catch (_: Exception) {
        }
    }

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            stopScan()
            prefs.watchAddress = result.device.address
            connect(result.device, autoConnect = false)
        }
    }

    private fun connect(device: BluetoothDevice, autoConnect: Boolean) {
        gatt?.close()
        _state.value = State.CONNECTING
        gatt = device.connectGatt(ctx, autoConnect, gattCallback, BluetoothDevice.TRANSPORT_LE)
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                failures = 0
                handler.post { setBackgroundScan(false) }
                handler.post { g.requestMtu(517) }
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                rx = null
                writeResult?.complete(false)
                g.close()
                if (gatt == g) gatt = null
                failures++
                if (!running) return
                handler.post { setBackgroundScan(true) }
                _state.value = State.CONNECTING
                // autoConnect=true: Android wartet selbst, bis die Uhr wieder auftaucht
                handler.postDelayed({ connectOrScan() }, if (failures > 5) 15_000L else 2_000L)
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, newMtu: Int, status: Int) {
            mtu = if (status == BluetoothGatt.GATT_SUCCESS) newMtu else 23
            handler.post { g.discoverServices() }
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            val svc = g.getService(SERVICE)
            val tx = svc?.getCharacteristic(TX)
            rx = svc?.getCharacteristic(RX)
            if (svc == null || tx == null || rx == null) {
                Log.w(TAG, "NUS nicht gefunden – falsches Gerät?")
                g.disconnect()
                return
            }
            g.setCharacteristicNotification(tx, true)
            val cccd = tx.getDescriptor(CCCD) ?: return
            if (Build.VERSION.SDK_INT >= 33) {
                g.writeDescriptor(cccd, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
            } else {
                @Suppress("DEPRECATION")
                cccd.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                @Suppress("DEPRECATION")
                g.writeDescriptor(cccd)
            }
        }

        override fun onDescriptorWrite(g: BluetoothGatt, d: BluetoothGattDescriptor, status: Int) {
            if (d.uuid == CCCD) {
                _state.value = State.CONNECTED  // Uhr schickt jetzt SYNC|all
                Log.i(TAG, "Verbunden, MTU $mtu")
            }
        }

        override fun onCharacteristicWrite(g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int) {
            writeResult?.complete(status == BluetoothGatt.GATT_SUCCESS)
        }

        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray) {
            if (c.uuid != TX) return
            // Notifications kommen nur nach erfolgreichem Abo -> SYNC kann vor onDescriptorWrite eintreffen
            if (rx != null) _state.value = State.CONNECTED
            onMessage?.invoke(String(value, Charsets.UTF_8))
        }

        @Deprecated("API < 33")
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic) {
            if (Build.VERSION.SDK_INT < 33 && c.uuid == TX) {
                if (rx != null) _state.value = State.CONNECTED
                @Suppress("DEPRECATION")
                onMessage?.invoke(String(c.value ?: return, Charsets.UTF_8))
            }
        }
    }

    companion object {
        private const val TAG = "BleLink"
        const val MAX_MESSAGE = 8000  // Uhr akzeptiert max. 8192 Byte pro Nachricht
        val SERVICE: UUID = UUID.fromString("6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
        val RX: UUID = UUID.fromString("6E400002-B5A3-F393-E0A9-E50E24DCCA9E")
        val TX: UUID = UUID.fromString("6E400003-B5A3-F393-E0A9-E50E24DCCA9E")
        val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }
}
