package de.olizei.watchbridge

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.location.Geocoder
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Looper
import android.util.Base64
import android.util.Log
import android.util.LruCache
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.net.HttpURLConnection
import java.net.URL
import java.util.Locale
import kotlin.math.PI
import kotlin.math.atan
import kotlin.math.cos
import kotlin.math.exp
import kotlin.math.floor
import kotlin.math.ln
import kotlin.math.tan

/**
 * Standort + Kartenbild für die Uhr.
 *
 *  - Schickt GPS|lat,lon|Ort bei Bewegung (> 50 m) bzw. alle paar Minuten.
 *  - Solange die Uhr die Karte zeigt (MAP_REQ, alle 20 s erneuert), wird ein
 *    240×240-Ausschnitt aus CARTO-Kacheln (OpenStreetMap-Daten) gerendert,
 *    auf 16 Farben reduziert und als LVGL-Bild (INDEXED_4BIT) übertragen.
 */
@SuppressLint("MissingPermission")
class MapProvider(private val ctx: Context, private val scope: CoroutineScope, private val bridge: Bridge) {

    private val lm = ctx.getSystemService(LocationManager::class.java)
    private var last: Location? = null
    private var lastGpsSent: Location? = null
    private var lastGpsMs = 0L

    // Kartenanforderung der Uhr
    private var zoom = 16
    private var dark = true
    private var leaseUntil = 0L
    private var lastMapAt: Location? = null
    private var lastMapKey = ""
    private var mapJob: Job? = null
    private var fastUpdates = false

    private val tiles = object : LruCache<String, Bitmap>(24) {}

    private val listener = LocationListener { onLocation(it) }

    fun start() {
        requestUpdates(fast = false)
        try {
            (lm.getLastKnownLocation(LocationManager.FUSED_PROVIDER)
                ?: lm.getLastKnownLocation(LocationManager.GPS_PROVIDER)
                ?: lm.getLastKnownLocation(LocationManager.NETWORK_PROVIDER))?.let { onLocation(it) }
        } catch (_: Exception) {
        }
    }

    fun shutdown() {
        try {
            lm.removeUpdates(listener)
        } catch (_: Exception) {
        }
        mapJob?.cancel()
    }

    private fun requestUpdates(fast: Boolean) {
        try {
            lm.removeUpdates(listener)
            // Karte offen: jede Sekunde / 5 m. Sonst sparsam: 2 min / 50 m.
            val interval = if (fast) 1000L else 120_000L
            val dist = if (fast) 5f else 50f
            val provider = when {
                lm.isProviderEnabled(LocationManager.FUSED_PROVIDER) -> LocationManager.FUSED_PROVIDER
                fast && lm.isProviderEnabled(LocationManager.GPS_PROVIDER) -> LocationManager.GPS_PROVIDER
                else -> LocationManager.NETWORK_PROVIDER
            }
            lm.requestLocationUpdates(provider, interval, dist, listener, Looper.getMainLooper())
            fastUpdates = fast
        } catch (e: Exception) {
            Log.w(TAG, "Kein Standortzugriff: ${e.message}")
        }
    }

    fun sendLast() {
        last?.let { sendGps(it) }
    }

    private fun onLocation(loc: Location) {
        last = loc
        val prev = lastGpsSent
        if (prev == null || prev.distanceTo(loc) > 50 || System.currentTimeMillis() - lastGpsMs > 5 * 60_000L) {
            sendGps(loc)
        }
        if (leasing() && (lastMapAt?.distanceTo(loc) ?: Float.MAX_VALUE) > 25f) renderSoon()
    }

    private fun sendGps(loc: Location) {
        lastGpsSent = loc
        lastGpsMs = System.currentTimeMillis()
        scope.launch(Dispatchers.IO) {
            val place = placeName(loc)
            bridge.send("GPS", "%.5f,%.5f".format(Locale.US, loc.latitude, loc.longitude), place)
        }
    }

    private fun placeName(loc: Location): String = try {
        @Suppress("DEPRECATION")
        val a = Geocoder(ctx, Locale.GERMANY).getFromLocation(loc.latitude, loc.longitude, 1)?.firstOrNull()
        listOfNotNull(
            a?.thoroughfare?.let { s -> a.subThoroughfare?.let { "$s $it" } ?: s },
            a?.locality,
        ).joinToString(", ").replace('|', '/')
    } catch (_: Exception) {
        ""
    }

    // --- Kartenbild -----------------------------------------------------------------

    private fun leasing() = System.currentTimeMillis() < leaseUntil

    /** MAP_REQ|zoom|dark – Uhr zeigt die Karte (wird alle 20 s erneuert). */
    fun lease(z: Int, darkMode: Boolean) {
        val changed = z != zoom || darkMode != dark || !leasing()
        zoom = z.coerceIn(3, 19)
        dark = darkMode
        leaseUntil = System.currentTimeMillis() + 45_000L
        if (!fastUpdates) requestUpdates(fast = true)
        if (changed) {
            lastMapKey = ""
            renderSoon()
        }
        // Lease-Ende überwachen -> zurück auf sparsame Updates
        scope.launch {
            delay(46_000L)
            if (!leasing() && fastUpdates) requestUpdates(fast = false)
        }
    }

    /** MAP_STOP – Uhr hat die Karte geschlossen. */
    fun endLease() {
        leaseUntil = 0
        if (fastUpdates) requestUpdates(fast = false)
    }

    private fun renderSoon() {
        if (mapJob?.isActive == true) return
        mapJob = scope.launch {
            val loc = last ?: return@launch
            val key = "%.4f,%.4f,%d,%b".format(Locale.US, loc.latitude, loc.longitude, zoom, dark)
            if (key == lastMapKey) return@launch
            try {
                val bmp = withContext(Dispatchers.IO) { render(loc.latitude, loc.longitude, zoom, dark) }
                val img = encodeIndexed4(bmp)
                bmp.recycle()
                if (!leasing()) return@launch
                sendImage(img)
                lastMapKey = key
                lastMapAt = loc
            } catch (e: Exception) {
                Log.w(TAG, "Karte fehlgeschlagen: ${e.message}")
            }
        }
    }

    private fun sendImage(data: ByteArray) {
        val b64 = Base64.encodeToString(data, Base64.NO_WRAP)
        bridge.send("MAP_BEGIN", SIZE.toString(), SIZE.toString(), data.size.toString())
        var i = 0
        while (i < b64.length) {
            val end = minOf(i + CHUNK, b64.length)  // CHUNK ist Vielfaches von 4
            bridge.send("MAP", b64.substring(i, end))
            i = end
        }
        bridge.send("MAP_END")
    }

    /** 240×240 px, Position in der Mitte. */
    private fun render(lat: Double, lon: Double, z: Int, darkMode: Boolean): Bitmap {
        val n = (1 shl z).toDouble()
        val px = (lon + 180.0) / 360.0 * n * 256.0
        val latR = lat * PI / 180.0
        val py = (1.0 - ln(tan(latR) + 1.0 / cos(latR)) / PI) / 2.0 * n * 256.0
        val left = px - SIZE / 2.0
        val top = py - SIZE / 2.0

        val out = Bitmap.createBitmap(SIZE, SIZE, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(out)
        canvas.drawColor(if (darkMode) 0xFF1C1C1E.toInt() else 0xFFF2F2F7.toInt())
        val tx0 = floor(left / 256).toInt()
        val ty0 = floor(top / 256).toInt()
        val tx1 = floor((left + SIZE - 1) / 256).toInt()
        val ty1 = floor((top + SIZE - 1) / 256).toInt()
        val max = 1 shl z
        for (ty in ty0..ty1) for (tx in tx0..tx1) {
            if (ty < 0 || ty >= max) continue
            val wx = ((tx % max) + max) % max
            val tile = tile(z, wx, ty, darkMode) ?: continue
            canvas.drawBitmap(tile, (tx * 256 - left).toFloat(), (ty * 256 - top).toFloat(), null)
        }
        return out
    }

    private fun tile(z: Int, x: Int, y: Int, darkMode: Boolean): Bitmap? {
        val style = if (darkMode) "dark_all" else "light_all"
        val key = "$style/$z/$x/$y"
        tiles.get(key)?.let { return it }
        val sub = "abcd"[(x + y) and 3]
        val c = URL("https://$sub.basemaps.cartocdn.com/$style/$z/$x/$y.png").openConnection() as HttpURLConnection
        c.connectTimeout = 10_000
        c.readTimeout = 10_000
        c.setRequestProperty("User-Agent", "WatchBridge/1.0 (Android; smartwatch companion)")
        return try {
            if (c.responseCode != 200) return null
            val bmp = c.inputStream.use { BitmapFactory.decodeStream(it) } ?: return null
            tiles.put(key, bmp)
            bmp
        } finally {
            c.disconnect()
        }
    }

    companion object {
        private const val TAG = "MapProvider"
        const val SIZE = 240
        const val CHUNK = 4000

        /**
         * LVGL LV_IMG_CF_INDEXED_4BIT: 16 Paletteneinträge à 4 Byte (B,G,R,A),
         * danach je Pixel 4 Bit (erstes Pixel im oberen Nibble).
         * Palette per Median-Cut auf einem 4-Bit-pro-Kanal-Histogramm.
         */
        fun encodeIndexed4(bmp: Bitmap): ByteArray {
            val w = bmp.width
            val h = bmp.height
            val px = IntArray(w * h)
            bmp.getPixels(px, 0, w, 0, 0, w, h)

            // Histogramm (12 Bit RGB)
            val hist = IntArray(4096)
            for (c in px) hist[key12(c)]++
            val colors = (0 until 4096).filter { hist[it] > 0 }.toMutableList()
            val boxes = mutableListOf(colors)
            while (boxes.size < 16) {
                // Box mit der größten Ausdehnung (gewichtet) teilen
                var bi = -1
                var bestRange = -1
                var bestCh = 0
                for ((i, b) in boxes.withIndex()) {
                    if (b.size < 2) continue
                    for (ch in 0..2) {
                        val vals = b.map { (it shr (8 - ch * 4)) and 15 }
                        val r = vals.max() - vals.min()
                        if (r > bestRange) {
                            bestRange = r; bi = i; bestCh = ch
                        }
                    }
                }
                if (bi < 0 || bestRange == 0) break
                val box = boxes.removeAt(bi).sortedBy { (it shr (8 - bestCh * 4)) and 15 }
                val total = box.sumOf { hist[it] }
                var acc = 0
                var cut = 1
                for ((i, c) in box.withIndex()) {
                    acc += hist[c]
                    if (acc >= total / 2) {
                        cut = (i + 1).coerceIn(1, box.size - 1); break
                    }
                }
                boxes += box.subList(0, cut).toMutableList()
                boxes += box.subList(cut, box.size).toMutableList()
            }

            // Palette = gewichteter Mittelwert je Box
            val pal = IntArray(16)
            for ((i, b) in boxes.withIndex()) {
                var r = 0L; var g = 0L; var bl = 0L; var n = 0L
                for (c in b) {
                    val k = hist[c].toLong()
                    r += ((c shr 8) and 15) * 17 * k
                    g += ((c shr 4) and 15) * 17 * k
                    bl += (c and 15) * 17 * k
                    n += k
                }
                if (n > 0) pal[i] = (0xFF shl 24) or ((r / n).toInt() shl 16) or ((g / n).toInt() shl 8) or (bl / n).toInt()
            }
            val used = boxes.size.coerceAtLeast(1)

            // Nächste Palettenfarbe je 12-Bit-Farbe (Cache)
            val lut = IntArray(4096) { -1 }
            fun nearest(k: Int): Int {
                if (lut[k] >= 0) return lut[k]
                val r = ((k shr 8) and 15) * 17
                val g = ((k shr 4) and 15) * 17
                val b = (k and 15) * 17
                var best = 0
                var bd = Int.MAX_VALUE
                for (i in 0 until used) {
                    val p = pal[i]
                    val dr = ((p shr 16) and 255) - r
                    val dg = ((p shr 8) and 255) - g
                    val db = (p and 255) - b
                    val d = 3 * dr * dr + 4 * dg * dg + 2 * db * db
                    if (d < bd) {
                        bd = d; best = i
                    }
                }
                lut[k] = best
                return best
            }

            val out = ByteArray(64 + w * h / 2)
            for (i in 0 until 16) {
                val p = pal[i]
                out[i * 4] = (p and 255).toByte()
                out[i * 4 + 1] = ((p shr 8) and 255).toByte()
                out[i * 4 + 2] = ((p shr 16) and 255).toByte()
                out[i * 4 + 3] = 0xFF.toByte()
            }
            var o = 64
            var i = 0
            while (i < px.size) {
                val hi = nearest(key12(px[i]))
                val lo = if (i + 1 < px.size) nearest(key12(px[i + 1])) else 0
                out[o++] = ((hi shl 4) or lo).toByte()
                i += 2
            }
            return out
        }

        private fun key12(c: Int): Int =
            (((c shr 20) and 15) shl 8) or (((c shr 12) and 15) shl 4) or ((c shr 4) and 15)

        @Suppress("unused")
        fun tileToLat(y: Double, z: Int): Double {
            val n = PI - 2.0 * PI * y / (1 shl z)
            return 180.0 / PI * atan(0.5 * (exp(n) - exp(-n)))
        }
    }
}
