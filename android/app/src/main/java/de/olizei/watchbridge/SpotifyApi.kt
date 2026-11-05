package de.olizei.watchbridge

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.util.Base64
import android.util.Log
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import org.json.JSONArray
import org.json.JSONObject
import java.io.IOException
import java.net.HttpURLConnection
import java.net.InetAddress
import java.net.ServerSocket
import java.net.URL
import java.net.URLEncoder
import java.security.MessageDigest
import java.security.SecureRandom
import kotlin.concurrent.thread

/**
 * Spotify Web API (Premium nötig): Suche, Wiedergabe am Handy, Geschmack des Nutzers.
 *
 * Login per OAuth PKCE (kein Client-Secret). Spotify erlaubt als Redirect nur HTTPS oder
 * Loopback – deshalb hört WatchBridge während des Logins kurz auf http://127.0.0.1:8898/callback.
 * Die Client-ID stammt aus einer eigenen App auf developer.spotify.com (Development Mode).
 */
class SpotifyApi(private val ctx: Context, private val prefs: Prefs) {

    private val _status = MutableStateFlow(if (prefs.spRefresh.isNotEmpty()) "Verbunden" else "Nicht verbunden")
    val status: StateFlow<String> = _status

    val connected: Boolean get() = prefs.spRefresh.isNotEmpty() && prefs.spClientId.isNotEmpty()

    private var accessToken = ""
    private var accessUntil = 0L

    class ApiException(msg: String) : IOException(msg)

    // --- Login ------------------------------------------------------------------------------------

    /** Öffnet den Spotify-Login im Browser; das Ergebnis landet in [status]. */
    fun login(activity: Context) {
        val clientId = prefs.spClientId
        if (clientId.isEmpty()) { _status.value = "Erst die Client-ID eintragen"; return }

        val verifier = randomString(64)
        val challenge = Base64.encodeToString(
            MessageDigest.getInstance("SHA-256").digest(verifier.toByteArray()),
            Base64.URL_SAFE or Base64.NO_PADDING or Base64.NO_WRAP,
        )
        val state = randomString(16)

        val server = try {
            ServerSocket(PORT, 1, InetAddress.getByName("127.0.0.1")).apply { soTimeout = 5 * 60_000 }
        } catch (e: IOException) {
            _status.value = "Login läuft schon – im Browser abschließen"
            return
        }
        _status.value = "Warte auf Spotify-Login im Browser …"

        thread(name = "spotify-login") {
            server.use { srv ->
                try {
                    while (true) {
                        val sock = srv.accept()
                        sock.use { s ->
                            val line = s.getInputStream().bufferedReader().readLine() ?: return@use
                            // "GET /callback?code=...&state=... HTTP/1.1"
                            val path = line.split(' ').getOrNull(1) ?: ""
                            if (!path.startsWith("/callback")) {
                                respond(s, 404, "Nicht gefunden"); return@use
                            }
                            val q = Uri.parse("http://x$path")
                            val code = q.getQueryParameter("code")
                            val ok = code != null && q.getQueryParameter("state") == state
                            val msg = if (ok) "Spotify ist verbunden. Du kannst zu WatchBridge zurückkehren."
                            else "Anmeldung abgebrochen (${q.getQueryParameter("error") ?: "ungültig"})."
                            respond(s, 200, msg)
                            if (ok) exchange(clientId, code!!, verifier) else _status.value = msg
                            return@thread
                        }
                    }
                } catch (e: Exception) {
                    Log.w(TAG, "Login", e)
                    _status.value = "Login fehlgeschlagen: ${e.message}"
                }
            }
        }

        val url = Uri.parse("https://accounts.spotify.com/authorize").buildUpon()
            .appendQueryParameter("client_id", clientId)
            .appendQueryParameter("response_type", "code")
            .appendQueryParameter("redirect_uri", REDIRECT)
            .appendQueryParameter("code_challenge_method", "S256")
            .appendQueryParameter("code_challenge", challenge)
            .appendQueryParameter("state", state)
            .appendQueryParameter("scope", SCOPES)
            .build()
        activity.startActivity(Intent(Intent.ACTION_VIEW, url).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
    }

    fun logout() {
        prefs.spRefresh = ""
        accessToken = ""
        accessUntil = 0
        _status.value = "Nicht verbunden"
    }

    private fun respond(s: java.net.Socket, code: Int, msg: String) {
        val html = "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>" +
            "<body style='font-family:sans-serif;background:#000;color:#fff;display:flex;align-items:center;" +
            "justify-content:center;height:90vh;text-align:center;padding:24px'><h2>$msg</h2>"
        val body = html.toByteArray()
        val out = s.getOutputStream()
        out.write(("HTTP/1.1 $code OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: ${body.size}\r\n" +
            "Connection: close\r\n\r\n").toByteArray())
        out.write(body)
        out.flush()
    }

    private fun exchange(clientId: String, code: String, verifier: String) {
        val r = tokenRequest(mapOf(
            "grant_type" to "authorization_code", "code" to code, "redirect_uri" to REDIRECT,
            "client_id" to clientId, "code_verifier" to verifier,
        ))
        storeTokens(r)
        val me = try { JSONObject(call("GET", "/me")).optString("display_name") } catch (_: Exception) { "" }
        _status.value = "Verbunden" + if (me.isNotEmpty()) " als $me" else ""
    }

    private fun storeTokens(r: JSONObject) {
        accessToken = r.getString("access_token")
        accessUntil = System.currentTimeMillis() + (r.optLong("expires_in", 3600) - 60) * 1000
        r.optString("refresh_token").takeIf { it.isNotEmpty() }?.let { prefs.spRefresh = it }
    }

    private fun tokenRequest(form: Map<String, String>): JSONObject {
        val c = URL("https://accounts.spotify.com/api/token").openConnection() as HttpURLConnection
        c.requestMethod = "POST"
        c.doOutput = true
        c.connectTimeout = 10_000
        c.readTimeout = 10_000
        c.setRequestProperty("Content-Type", "application/x-www-form-urlencoded")
        c.outputStream.use { it.write(form.entries.joinToString("&") { (k, v) -> "$k=${enc(v)}" }.toByteArray()) }
        val text = (if (c.responseCode in 200..299) c.inputStream else c.errorStream)?.bufferedReader()?.readText() ?: ""
        if (c.responseCode !in 200..299) {
            if (c.responseCode == 400 && text.contains("invalid_grant")) logout()
            throw ApiException("Spotify-Anmeldung abgelaufen – bitte in WatchBridge neu verbinden")
        }
        return JSONObject(text)
    }

    @Synchronized
    private fun token(): String {
        if (accessToken.isNotEmpty() && System.currentTimeMillis() < accessUntil) return accessToken
        if (!connected) throw ApiException("Spotify ist nicht verbunden (WatchBridge → KI-Assistent → Spotify)")
        storeTokens(tokenRequest(mapOf(
            "grant_type" to "refresh_token", "refresh_token" to prefs.spRefresh, "client_id" to prefs.spClientId,
        )))
        return accessToken
    }

    // --- Web API ----------------------------------------------------------------------------------

    private fun call(method: String, path: String, body: JSONObject? = null): String {
        val c = URL("https://api.spotify.com/v1$path").openConnection() as HttpURLConnection
        c.requestMethod = method
        c.connectTimeout = 10_000
        c.readTimeout = 15_000
        c.setRequestProperty("Authorization", "Bearer ${token()}")
        if (body != null || method == "PUT" || method == "POST") {
            c.doOutput = true
            c.setRequestProperty("Content-Type", "application/json")
            c.outputStream.use { it.write((body?.toString() ?: "").toByteArray()) }
        }
        val code = c.responseCode
        val text = (if (code in 200..299) c.inputStream else c.errorStream)?.bufferedReader()?.readText() ?: ""
        if (code !in 200..299) {
            val msg = try { JSONObject(text).getJSONObject("error").optString("message") } catch (_: Exception) { text.take(120) }
            throw ApiException(when (code) {
                401 -> "Spotify-Anmeldung ungültig – bitte neu verbinden"
                403 -> "Spotify verweigert das ($msg) – Premium nötig bzw. Konto in der Developer-App freigeben"
                404 -> "Kein aktives Spotify-Gerät ($msg)"
                429 -> "Spotify-Limit erreicht, später nochmal"
                else -> "Spotify-Fehler $code: $msg"
            })
        }
        return text
    }

    private fun artists(o: JSONObject): String {
        val a = o.optJSONArray("artists") ?: return ""
        return (0 until a.length()).joinToString(", ") { a.getJSONObject(it).optString("name") }
    }

    /** Suche, max. 10 Treffer (Development Mode). type: track, artist, album, playlist. */
    fun search(query: String, type: String, limit: Int = 8): String {
        val t = if (type in listOf("track", "artist", "album", "playlist")) type else "track"
        val j = JSONObject(call("GET", "/search?type=$t&limit=${limit.coerceIn(1, 10)}&market=from_token&q=${enc(query)}"))
        val items = j.optJSONObject("${t}s")?.optJSONArray("items") ?: JSONArray()
        val lines = (0 until items.length()).mapNotNull { i ->
            val o = items.optJSONObject(i) ?: return@mapNotNull null   // Playlists können null sein
            when (t) {
                "track" -> "${o.optString("name")} – ${artists(o)} | ${o.optString("uri")}"
                "album" -> "${o.optString("name")} – ${artists(o)} | ${o.optString("uri")}"
                "playlist" -> "${o.optString("name")} (von ${o.optJSONObject("owner")?.optString("display_name")}) | ${o.optString("uri")}"
                else -> "${o.optString("name")} | ${o.optString("uri")}"
            }
        }
        return if (lines.isEmpty()) "Keine Treffer" else lines.joinToString("\n")
    }

    /** Geschmack: Top-Künstler/-Songs, zuletzt gehört, eigene Playlists – jeweils mit URI. */
    fun taste(): String {
        val sb = StringBuilder()
        fun section(title: String, block: () -> String) {
            try { sb.append(title).append(":\n").append(block()).append("\n\n") } catch (e: ApiException) {
                sb.append(title).append(": nicht verfügbar (${e.message})\n\n")
            }
        }
        section("Top-Künstler (letzte Monate)") {
            val a = JSONObject(call("GET", "/me/top/artists?limit=25&time_range=medium_term")).getJSONArray("items")
            (0 until a.length()).joinToString("; ") { a.getJSONObject(it).let { o -> "${o.optString("name")} ${o.optString("uri")}" } }
        }
        section("Top-Songs (letzte Wochen)") {
            val a = JSONObject(call("GET", "/me/top/tracks?limit=25&time_range=short_term")).getJSONArray("items")
            (0 until a.length()).joinToString("\n") { a.getJSONObject(it).let { o -> "${o.optString("name")} – ${artists(o)} | ${o.optString("uri")}" } }
        }
        section("Zuletzt gehört") {
            val a = JSONObject(call("GET", "/me/player/recently-played?limit=20")).getJSONArray("items")
            (0 until a.length()).map { a.getJSONObject(it).getJSONObject("track") }.distinctBy { it.optString("uri") }
                .joinToString("\n") { o -> "${o.optString("name")} – ${artists(o)} | ${o.optString("uri")}" }
        }
        section("Eigene Playlists") {
            val a = JSONObject(call("GET", "/me/playlists?limit=30")).getJSONArray("items")
            (0 until a.length()).mapNotNull { a.optJSONObject(it) }
                .joinToString("\n") { o -> "${o.optString("name")} | ${o.optString("uri")}" }
        }
        return sb.toString().trim()
    }

    /** Spielt Songs ([uris]) oder einen Kontext (Album/Playlist/Künstler) auf dem Handy. */
    fun play(uris: List<String>, context: String?, shuffle: Boolean?): String {
        val device = phoneDevice() ?: run {
            // Handy taucht in Spotify Connect nur auf, solange die App wach ist -> über die
            // Medien-Session wecken (klappt im Hintergrund), sonst die App starten
            val woke = WatchBridgeApp.instance.bridge.media?.wakeSpotify() == true
            if (!woke) startSpotify()
            var d: Pair<String, Boolean>? = null
            for (i in 0 until 20) {
                Thread.sleep(500)
                d = phoneDevice()
                if (d != null) break
            }
            d ?: throw ApiException("Spotify am Handy ist nicht erreichbar – bitte Spotify einmal öffnen")
        }
        // Schlafendes Gerät erst übernehmen, sonst nimmt Spotify den Befehl an und spielt trotzdem nichts
        if (!device.second) {
            call("PUT", "/me/player", JSONObject().put("device_ids", JSONArray(listOf(device.first))).put("play", false))
            Thread.sleep(800)
        }
        val id = device.first
        shuffle?.let { call("PUT", "/me/player/shuffle?state=$it&device_id=$id") }
        val body = JSONObject()
        if (context != null) body.put("context_uri", context) else body.put("uris", JSONArray(uris.take(50)))
        call("PUT", "/me/player/play?device_id=$id", body)
        waitPlaying()?.let { return "Läuft jetzt: $it" }

        // Spotify im Hintergrund darf unter Android oft nicht selbst losspielen -> kurz in den Vordergrund holen
        Log.d(TAG, "Keine Wiedergabe – Spotify in den Vordergrund und nochmal")
        if (!startSpotify()) return "Spotify hat den Befehl angenommen, spielt aber nicht – Spotify einmal öffnen"
        Thread.sleep(2500)
        call("PUT", "/me/player/play?device_id=$id", body)
        return waitPlaying()?.let { "Läuft jetzt: $it" } ?: "Befehl gesendet, Spotify meldet aber noch keine Wiedergabe"
    }

    private fun waitPlaying(): String? {
        for (i in 0 until 6) {
            Thread.sleep(700)
            nowPlaying()?.let { return it }
        }
        return null
    }

    /** Letzte Wiedergabe auf dem Handy fortsetzen (z.B. von der Uhr aus, wenn Spotify beendet war). */
    fun resume(): String {
        val device = phoneDevice() ?: run {
            startSpotify()
            var d: Pair<String, Boolean>? = null
            for (i in 0 until 16) {
                Thread.sleep(500)
                d = phoneDevice()
                if (d != null) break
            }
            d ?: throw ApiException("Spotify am Handy ist nicht erreichbar")
        }
        call("PUT", "/me/player/play?device_id=${device.first}")
        return "Fortgesetzt"
    }

    fun queue(uri: String): String {
        call("POST", "/me/player/queue?uri=${enc(uri)}")
        return "In die Warteschlange gelegt"
    }

    private fun nowPlaying(): String? = try {
        val t = call("GET", "/me/player/currently-playing")
        if (t.isBlank()) null else JSONObject(t).takeIf { it.optBoolean("is_playing") }
            ?.optJSONObject("item")?.let { "${it.optString("name")} – ${artists(it)}" }
    } catch (_: Exception) { null }

    /** Geräte-ID des Handys in Spotify Connect, null = Spotify am Handy gerade nicht verbunden. */
    private fun phoneDevice(): Pair<String, Boolean>? {
        val a = JSONObject(call("GET", "/me/player/devices")).optJSONArray("devices") ?: return null
        val list = (0 until a.length()).map { a.getJSONObject(it) }
        Log.d(TAG, "Geräte: " + list.joinToString { "${it.optString("name")}/${it.optString("type")}/${it.optBoolean("is_active")}" })
        // Nur das Handy – nie ungefragt auf Fernseher/PC abspielen
        val d = list.firstOrNull { it.optString("type").equals("Smartphone", true) }
        val id = d?.optString("id")?.takeIf { it.isNotEmpty() } ?: return null
        return id to d.optBoolean("is_active")
    }

    private fun startSpotify(): Boolean = try {
        val i = ctx.packageManager.getLaunchIntentForPackage(MediaWatcher.SPOTIFY)
        if (i != null) ctx.startActivity(i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
        i != null
    } catch (e: Exception) {
        Log.w(TAG, "Spotify starten", e)
        false
    }

    companion object {
        private const val TAG = "SpotifyApi"
        const val PORT = 8898
        const val REDIRECT = "http://127.0.0.1:8898/callback"
        private const val SCOPES = "user-read-playback-state user-modify-playback-state user-read-currently-playing " +
            "user-top-read user-read-recently-played user-library-read playlist-read-private playlist-read-collaborative"

        private fun enc(s: String) = URLEncoder.encode(s, "UTF-8").replace("+", "%20")
        private fun randomString(n: Int): String {
            val chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
            val r = SecureRandom()
            return String(CharArray(n) { chars[r.nextInt(chars.length)] })
        }
    }
}
