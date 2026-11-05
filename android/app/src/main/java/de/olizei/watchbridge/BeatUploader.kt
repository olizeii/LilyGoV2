package de.olizei.watchbridge

import android.media.AudioFormat
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import android.provider.OpenableColumns
import android.util.Base64
import android.util.Log
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull
import java.io.ByteArrayOutputStream
import java.nio.ByteOrder
import kotlin.math.abs
import kotlin.math.min

/**
 * Lädt einen Beat (beliebige Audiodatei) ins Studio der Uhr:
 * dekodieren -> mono 16 kHz -> normalisieren -> µ-law (1 Byte/Sample) -> per BLE.
 * Flusskontrolle: die Uhr bestätigt alle [WINDOW] Pakete mit BEAT_ACK|n.
 */
class BeatUploader(private val app: WatchBridgeApp) {

    private val _status = MutableStateFlow("")
    val status: StateFlow<String> = _status
    private val _busy = MutableStateFlow(false)
    val busy: StateFlow<Boolean> = _busy

    @Volatile private var ackWait: Pair<String, CompletableDeferred<Unit>>? = null
    @Volatile private var failed = false
    private var job: Job? = null

    /** Von der Bridge: BEAT_ACK|<n|done|-1> */
    fun onAck(arg: String) {
        if (arg == "-1") failed = true
        val w = ackWait ?: return
        val ok = arg == "-1" || arg == w.first ||
            (arg.toIntOrNull() != null && w.first.toIntOrNull() != null && arg.toInt() >= w.first.toInt())
        if (ok) w.second.complete(Unit)
    }

    fun upload(uri: Uri) {
        if (_busy.value) return
        job = app.scope.launch {
            _busy.value = true
            try {
                if (app.link.state.value != BleLink.State.CONNECTED) {
                    _status.value = "Uhr nicht verbunden"
                    return@launch
                }
                _status.value = "Wandle um …"
                val name = displayName(uri).substringBeforeLast('.').take(28).replace('|', ' ')
                val pcm = decode(uri)
                if (pcm.isEmpty()) {
                    _status.value = "Datei konnte nicht gelesen werden"
                    return@launch
                }
                val data = toUlaw(pcm)
                send(name, data)
            } catch (e: Exception) {
                Log.w(TAG, "Beat-Upload", e)
                _status.value = "Fehler: ${e.message}"
            } finally {
                ackWait = null
                _busy.value = false
            }
        }
    }

    private suspend fun send(name: String, data: ByteArray) {
        failed = false
        val bridge = app.bridge
        val secs = data.size / RATE
        // Ack schon vor dem Senden scharf schalten (die Uhr antwortet schnell)
        val d0 = CompletableDeferred<Unit>(); ackWait = "0" to d0
        bridge.send("BEAT_BEGIN", name, data.size.toString())
        if (withTimeoutOrNull(8000) { d0.await() } == null) {
            _status.value = "Uhr antwortet nicht – Firmware aktuell?"
            return
        }
        var chunk = 0
        var i = 0
        val total = data.size
        while (i < total) {
            val end = min(i + RAW_CHUNK, total)
            val nextChunk = chunk + 1
            val needAck = nextChunk % WINDOW == 0
            var d: CompletableDeferred<Unit>? = null
            if (needAck) { d = CompletableDeferred(); ackWait = nextChunk.toString() to d }
            bridge.send("BEAT", Base64.encodeToString(data, i, end - i, Base64.NO_WRAP))
            chunk = nextChunk
            i = end
            _status.value = "Sende „$name“ … ${100L * i / total} %"
            if (d != null) {
                val ok = withTimeoutOrNull(15000) { d.await() } != null && !failed
                ackWait = null
                if (!ok) {
                    _status.value = "Übertragung abgebrochen"
                    return
                }
            }
            if (app.link.state.value != BleLink.State.CONNECTED) {
                _status.value = "Verbindung verloren"
                return
            }
        }
        val dEnd = CompletableDeferred<Unit>(); ackWait = "done" to dEnd
        bridge.send("BEAT_END")
        val ok = withTimeoutOrNull(20000) { dEnd.await() } != null && !failed
        _status.value = if (ok) "„$name“ ist auf der Uhr (${secs / 60}:${"%02d".format(secs % 60)})" else "Speichern auf der Uhr fehlgeschlagen"
    }

    private fun displayName(uri: Uri): String {
        app.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
            if (c.moveToFirst()) return c.getString(0) ?: "Beat"
        }
        return uri.lastPathSegment ?: "Beat"
    }

    /** Dekodiert die Datei zu mono 16 kHz (Float -1..1), max. [MAX_SECONDS]. */
    private fun decode(uri: Uri): FloatArray {
        val ex = MediaExtractor()
        ex.setDataSource(app, uri, null)
        val track = (0 until ex.trackCount).firstOrNull {
            ex.getTrackFormat(it).getString(MediaFormat.KEY_MIME)?.startsWith("audio/") == true
        } ?: return FloatArray(0)
        ex.selectTrack(track)
        val fmt = ex.getTrackFormat(track)
        val codec = MediaCodec.createDecoderByType(fmt.getString(MediaFormat.KEY_MIME)!!)
        codec.configure(fmt, null, null, 0)
        codec.start()

        var rate = fmt.getInteger(MediaFormat.KEY_SAMPLE_RATE)
        var channels = fmt.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
        var floatPcm = false
        var mono = FloatArray(1 shl 16)
        var count = 0
        fun add(v: Float) {
            if (count == mono.size) mono = mono.copyOf(mono.size * 2)
            mono[count++] = v
        }
        val limitIn = { (MAX_SECONDS + 1) * rate }
        val info = MediaCodec.BufferInfo()
        var inDone = false
        var outDone = false
        try {
            while (!outDone && count < limitIn()) {
                if (!inDone) {
                    val ib = codec.dequeueInputBuffer(10_000)
                    if (ib >= 0) {
                        val buf = codec.getInputBuffer(ib)!!
                        val n = ex.readSampleData(buf, 0)
                        if (n < 0) {
                            codec.queueInputBuffer(ib, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                            inDone = true
                        } else {
                            codec.queueInputBuffer(ib, 0, n, ex.sampleTime, 0)
                            ex.advance()
                        }
                    }
                }
                val ob = codec.dequeueOutputBuffer(info, 10_000)
                when {
                    ob == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {
                        val of = codec.outputFormat
                        rate = of.getInteger(MediaFormat.KEY_SAMPLE_RATE)
                        channels = of.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
                        floatPcm = of.containsKey(MediaFormat.KEY_PCM_ENCODING) &&
                            of.getInteger(MediaFormat.KEY_PCM_ENCODING) == AudioFormat.ENCODING_PCM_FLOAT
                    }
                    ob >= 0 -> {
                        val buf = codec.getOutputBuffer(ob)!!.order(ByteOrder.nativeOrder())
                        buf.position(info.offset)
                        buf.limit(info.offset + info.size)
                        if (floatPcm) {
                            val fb = buf.asFloatBuffer()
                            while (fb.remaining() >= channels) {
                                var s = 0f
                                repeat(channels) { s += fb.get() }
                                add(s / channels)
                            }
                        } else {
                            val sb = buf.asShortBuffer()
                            while (sb.remaining() >= channels) {
                                var s = 0f
                                repeat(channels) { s += sb.get() / 32768f }
                                add(s / channels)
                            }
                        }
                        codec.releaseOutputBuffer(ob, false)
                        if (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0) outDone = true
                    }
                }
            }
        } finally {
            codec.stop(); codec.release(); ex.release()
        }

        // Auf 16 kHz umrechnen (linear interpoliert, vorher einfacher Tiefpass gegen Aliasing)
        val src = mono.copyOf(count)
        if (rate > RATE * 1.2) {
            val k = (rate / RATE).coerceAtLeast(1)
            for (j in src.indices.reversed()) {
                var s = 0f; var c = 0
                for (m in 0 until k) { val idx = j - m; if (idx >= 0) { s += src[idx]; c++ } }
                src[j] = s / c
            }
        }
        val outLen = min((src.size.toLong() * RATE / rate).toInt(), MAX_SECONDS * RATE)
        val out = FloatArray(outLen)
        val step = rate.toDouble() / RATE
        for (j in 0 until outLen) {
            val pos = j * step
            val a = pos.toInt()
            val f = (pos - a).toFloat()
            val x0 = src.getOrElse(a) { 0f }
            val x1 = src.getOrElse(a + 1) { x0 }
            out[j] = x0 + (x1 - x0) * f
        }
        return out
    }

    private fun toUlaw(pcm: FloatArray): ByteArray {
        val peak = pcm.maxOfOrNull { abs(it) }?.coerceAtLeast(0.01f) ?: 1f
        val g = 0.95f / peak
        val bos = ByteArrayOutputStream(pcm.size)
        for (x in pcm) bos.write(ulaw((x * g * 32767).toInt().coerceIn(-32767, 32767)).toInt())
        return bos.toByteArray()
    }

    private fun ulaw(pcm: Int): Byte {
        val sign = if (pcm < 0) 0x80 else 0
        var v = abs(pcm).coerceAtMost(32635) + 0x84
        var exp = 7
        var mask = 0x4000
        while (v and mask == 0 && exp > 0) { exp--; mask = mask shr 1 }
        val mant = (v shr (exp + 3)) and 0x0F
        return (sign or (exp shl 4) or mant).inv().toByte()
    }

    companion object {
        private const val TAG = "BeatUploader"
        const val RATE = 16000
        const val MAX_SECONDS = 40
        private const val RAW_CHUNK = 3000   // -> 4000 Zeichen Base64
        private const val WINDOW = 8         // wie auf der Uhr
    }
}
