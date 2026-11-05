package de.olizei.watchbridge

import android.annotation.SuppressLint
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import android.util.Log
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.io.DataOutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Song-Erkennung ("Was läuft gerade?"): nimmt ein paar Sekunden mit dem Handy-Mikro auf
 * und fragt AudD (api.audd.io). Blockierend – aus dem KI-Werkzeug-Thread aufrufen.
 */
class SongRecognizer(private val app: WatchBridgeApp) {

    /** @return Text für die KI + ob es ein Fehler war */
    fun identify(seconds: Int = 9): Pair<String, Boolean> {
        val token = app.prefs.auddToken.trim()
        if (token.isEmpty()) return ("Song-Erkennung ist nicht eingerichtet: in WatchBridge unter „Song-Erkennung“ " +
            "einen kostenlosen AudD-Token eintragen (dashboard.audd.io).") to true
        val wav = record(seconds) ?: return "Mikrofon am Handy nicht verfügbar" to true
        return try {
            parse(upload(token, wav))
        } catch (e: Exception) {
            Log.w(TAG, "AudD", e)
            "Erkennung fehlgeschlagen: ${e.message}" to true
        }
    }

    @SuppressLint("MissingPermission")
    private fun record(seconds: Int): ByteArray? {
        val min = AudioRecord.getMinBufferSize(RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT)
        val rec = try {
            AudioRecord(MediaRecorder.AudioSource.MIC,
                RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT, maxOf(min, RATE))
        } catch (e: Exception) {
            Log.w(TAG, "AudioRecord", e); return null
        }
        if (rec.state != AudioRecord.STATE_INITIALIZED) { rec.release(); return null }
        val pcm = ByteArray(RATE * 2 * seconds)
        try {
            rec.startRecording()
            var off = 0
            while (off < pcm.size) {
                val n = rec.read(pcm, off, minOf(4096, pcm.size - off))
                if (n <= 0) break
                off += n
            }
        } finally {
            rec.stop(); rec.release()
        }
        // Stille? (z.B. Mikro im Hintergrund blockiert)
        val sb = ByteBuffer.wrap(pcm).order(ByteOrder.LITTLE_ENDIAN).asShortBuffer()
        var peak = 0
        while (sb.hasRemaining()) peak = maxOf(peak, kotlin.math.abs(sb.get().toInt()))
        if (peak < 50) return null
        return wav(pcm)
    }

    private fun wav(pcm: ByteArray): ByteArray {
        val h = ByteBuffer.allocate(44).order(ByteOrder.LITTLE_ENDIAN)
        h.put("RIFF".toByteArray()).putInt(36 + pcm.size).put("WAVE".toByteArray())
        h.put("fmt ".toByteArray()).putInt(16).putShort(1).putShort(1).putInt(RATE).putInt(RATE * 2)
            .putShort(2).putShort(16)
        h.put("data".toByteArray()).putInt(pcm.size)
        return h.array() + pcm
    }

    private fun upload(token: String, wav: ByteArray): String {
        val boundary = "----wb" + System.nanoTime()
        val c = URL("https://api.audd.io/").openConnection() as HttpURLConnection
        c.requestMethod = "POST"
        c.doOutput = true
        c.connectTimeout = 10000
        c.readTimeout = 20000
        c.setRequestProperty("Content-Type", "multipart/form-data; boundary=$boundary")
        DataOutputStream(c.outputStream).use { o ->
            fun field(name: String, value: String) {
                o.writeBytes("--$boundary\r\nContent-Disposition: form-data; name=\"$name\"\r\n\r\n")
                o.write(value.toByteArray()); o.writeBytes("\r\n")
            }
            field("api_token", token)
            field("return", "spotify")
            o.writeBytes("--$boundary\r\nContent-Disposition: form-data; name=\"file\"; filename=\"clip.wav\"\r\n" +
                "Content-Type: audio/wav\r\n\r\n")
            o.write(wav)
            o.writeBytes("\r\n--$boundary--\r\n")
        }
        val stream = if (c.responseCode in 200..299) c.inputStream else c.errorStream
        return stream.bufferedReader().use { it.readText() }.also { c.disconnect() }
    }

    private fun parse(body: String): Pair<String, Boolean> {
        val j = JSONObject(body)
        if (j.optString("status") != "success") {
            val err = j.optJSONObject("error")
            return when (err?.optInt("error_code")) {
                900 -> "AudD-Token ungültig – in WatchBridge prüfen" to true
                901 -> "Kostenloses AudD-Kontingent aufgebraucht" to true
                else -> "AudD-Fehler: ${err?.optString("error_message") ?: body.take(120)}" to true
            }
        }
        val r = j.optJSONObject("result") ?: return ("Kein Song erkannt (zu leise, zu kurz oder unbekannt). " +
            "Tipp: Handy näher an die Musik halten und nochmal fragen.") to false
        val sb = StringBuilder("Erkannt: ${r.optString("title")} – ${r.optString("artist")}")
        r.optString("album").takeIf { it.isNotEmpty() }?.let { sb.append("\nAlbum: $it") }
        r.optString("release_date").takeIf { it.isNotEmpty() }?.let { sb.append("\nVeröffentlicht: $it") }
        r.optJSONObject("spotify")?.optString("uri")?.takeIf { it.isNotEmpty() }?.let { sb.append("\nSpotify: $it") }
        return sb.toString() to false
    }

    companion object {
        private const val TAG = "SongRecognizer"
        private const val RATE = 16000
    }
}
