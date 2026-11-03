package de.olizei.watchbridge

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import java.time.DayOfWeek
import java.time.LocalDate
import java.time.LocalTime
import java.time.format.DateTimeFormatter
import java.util.Locale

/**
 * Stundenplan über die öffentliche WebUntis-JSON-RPC-API
 * (dieselbe, die auch die Untis-Mobile-Web-App nutzt).
 *
 * Holt den heutigen Tag – oder den nächsten Schultag, wenn heute schon vorbei ist –
 * und schickt ihn als UNTIS|... an die Uhr. Aktualisierung alle 30 min und auf Anfrage.
 */
class UntisSync(private val scope: CoroutineScope, private val prefs: Prefs, private val bridge: Bridge) {

    data class Lesson(
        val date: Int, val start: Int, val end: Int,
        val subject: String, val room: String, val teacher: String,
        val status: Char, val info: String,
    )

    private val _status = MutableStateFlow("Nicht eingerichtet")
    val status: StateFlow<String> = _status

    private var cachedMessage: String? = null
    private var lastFetch = 0L
    private var loop: Job? = null

    fun start() {
        loop?.cancel()
        loop = scope.launch {
            while (isActive) {
                refreshNow()
                delay(30 * 60_000L)
            }
        }
    }

    fun stop() {
        loop?.cancel()
    }

    fun sendCached() {
        cachedMessage?.let { bridge.sendRaw(it) }
    }

    /** Auf Anfrage der Uhr – höchstens alle 5 Minuten wirklich abrufen. */
    fun refresh(force: Boolean) {
        if (!force && System.currentTimeMillis() - lastFetch < 5 * 60_000L) {
            sendCached()
            return
        }
        scope.launch { refreshNow() }
    }

    suspend fun refreshNow(): Boolean {
        if (!prefs.untisConfigured) {
            _status.value = "Nicht eingerichtet"
            return false
        }
        return try {
            _status.value = "Lade …"
            val (day, lessons) = withContext(Dispatchers.IO) { fetchRelevantDay() }
            lastFetch = System.currentTimeMillis()
            val msg = encode(day, lessons)
            cachedMessage = msg
            bridge.sendRaw(msg)
            val t = LocalTime.now().format(DateTimeFormatter.ofPattern("HH:mm"))
            _status.value = "OK – ${lessons.size} Stunden (${title(day)}), $t"
            true
        } catch (e: Exception) {
            _status.value = "Fehler: ${e.message ?: e.javaClass.simpleName}"
            false
        }
    }

    // --- Abruf --------------------------------------------------------------------

    private fun fetchRelevantDay(): Pair<LocalDate, List<Lesson>> {
        val session = Session(prefs)
        try {
            session.login()
            var day = LocalDate.now()
            var lessons = session.timetable(day)
            // Heute vorbei (oder Wochenende/frei) -> nächsten Schultag zeigen
            val nowMin = LocalTime.now().let { it.hour * 60 + it.minute }
            val todayDone = lessons.none { it.status != 'C' && toMin(it.end) > nowMin }
            var tries = 0
            if (todayDone) {
                do {
                    day = nextSchoolDay(day)
                    lessons = session.timetable(day)
                    tries++
                } while (lessons.isEmpty() && tries < 7)
                if (lessons.isEmpty()) {  // Ferien: heute anzeigen
                    day = LocalDate.now()
                    lessons = session.timetable(day)
                }
            }
            return day to lessons
        } finally {
            session.logout()
        }
    }

    private class Session(private val prefs: Prefs) {
        private var cookie = ""
        private var personId = 0
        private var personType = 0
        private var klasseId = 0
        private var id = 0

        private val url: String
            get() {
                val host = prefs.untisServer.removePrefix("https://").removePrefix("http://").trimEnd('/')
                    .substringBefore('/')
                return "https://$host/WebUntis/jsonrpc.do?school=" + URLEncoder.encode(prefs.untisSchool, "UTF-8")
            }

        fun call(method: String, params: Any): Any? {
            val body = JSONObject()
                .put("id", (++id).toString())
                .put("method", method)
                .put("params", params)
                .put("jsonrpc", "2.0")
            val c = URL(url).openConnection() as HttpURLConnection
            c.requestMethod = "POST"
            c.connectTimeout = 15_000
            c.readTimeout = 20_000
            c.doOutput = true
            c.setRequestProperty("Content-Type", "application/json")
            c.setRequestProperty("User-Agent", "WatchBridge/1.0")
            if (cookie.isNotEmpty()) c.setRequestProperty("Cookie", cookie)
            c.outputStream.use { it.write(body.toString().toByteArray()) }
            val text = (if (c.responseCode < 400) c.inputStream else c.errorStream).bufferedReader().use { it.readText() }
            c.disconnect()
            val json = JSONObject(text)
            json.optJSONObject("error")?.let { err ->
                val code = err.optInt("code")
                throw IllegalStateException(
                    when (code) {
                        -8504 -> "Benutzername oder Passwort falsch"
                        -8500 -> "Schule nicht gefunden"
                        -8520 -> "Nicht angemeldet"
                        else -> err.optString("message", "Untis-Fehler $code")
                    },
                )
            }
            return json.opt("result")
        }

        fun login() {
            val r = call(
                "authenticate",
                JSONObject().put("user", prefs.untisUser).put("password", prefs.untisPassword).put("client", "WatchBridge"),
            ) as? JSONObject ?: throw IllegalStateException("Anmeldung fehlgeschlagen")
            cookie = "JSESSIONID=" + r.getString("sessionId")
            personId = r.optInt("personId")
            personType = r.optInt("personType")
            klasseId = r.optInt("klasseId")
        }

        fun logout() {
            if (cookie.isEmpty()) return
            try {
                call("logout", JSONObject())
            } catch (_: Exception) {
            }
        }

        fun timetable(day: LocalDate): List<Lesson> {
            val d = day.format(DateTimeFormatter.BASIC_ISO_DATE).toInt()
            // Eigener Plan (Schüler/Lehrer); ohne Person-ID den Klassenplan
            val element = if (personId > 0 && personType > 0) {
                JSONObject().put("id", personId).put("type", personType)
            } else {
                JSONObject().put("id", klasseId).put("type", 1)
            }
            val options = JSONObject()
                .put("element", element)
                .put("startDate", d).put("endDate", d)
                .put("showInfo", true).put("showSubstText", true).put("showLsText", true)
                .put("subjectFields", JSONArray(listOf("name", "longname")))
                .put("roomFields", JSONArray(listOf("name")))
                .put("teacherFields", JSONArray(listOf("name")))
            val arr = call("getTimetable", JSONObject().put("options", options)) as? JSONArray ?: return emptyList()
            val out = ArrayList<Lesson>()
            for (i in 0 until arr.length()) {
                val p = arr.getJSONObject(i)
                val code = p.optString("code")
                val lstype = p.optString("lstype")
                val status = when {
                    code == "cancelled" -> 'C'
                    lstype == "ex" -> 'E'
                    code == "irregular" -> 'S'
                    else -> 'N'
                }
                val info = listOf(p.optString("substText"), p.optString("info"), p.optString("lstext"))
                    .firstOrNull { it.isNotBlank() } ?: ""
                out += Lesson(
                    date = p.optInt("date"),
                    start = p.optInt("startTime"),
                    end = p.optInt("endTime"),
                    subject = names(p.optJSONArray("su"), preferLong = false),
                    room = names(p.optJSONArray("ro"), preferLong = false),
                    teacher = names(p.optJSONArray("te"), preferLong = false),
                    status = status,
                    info = info,
                )
            }
            return merge(out.sortedWith(compareBy({ it.start }, { it.status == 'C' })))
        }

        private fun names(a: JSONArray?, preferLong: Boolean): String {
            if (a == null) return ""
            return (0 until a.length()).joinToString(", ") {
                val o = a.getJSONObject(it)
                if (preferLong) o.optString("longname", o.optString("name")) else o.optString("name")
            }
        }
    }

    companion object {
        private val GERMAN = Locale.GERMAN

        fun toMin(hhmm: Int) = hhmm / 100 * 60 + hhmm % 100
        private fun hm(t: Int) = "%02d:%02d".format(t / 100, t % 100)

        /** Aufeinanderfolgende gleiche Stunden (Doppelstunde) zusammenfassen. */
        fun merge(list: List<Lesson>): List<Lesson> {
            val out = ArrayList<Lesson>()
            for (l in list) {
                val prev = out.lastOrNull()
                val gap = if (prev != null) toMin(l.start) - toMin(prev.end) else 99
                if (prev != null && gap in 0..10 && prev.subject == l.subject && prev.room == l.room &&
                    prev.status == l.status && prev.teacher == l.teacher
                ) {
                    out[out.size - 1] = prev.copy(end = l.end, info = prev.info.ifBlank { l.info })
                } else {
                    out += l
                }
            }
            return out
        }

        fun nextSchoolDay(d: LocalDate): LocalDate {
            var n = d.plusDays(1)
            while (n.dayOfWeek == DayOfWeek.SATURDAY || n.dayOfWeek == DayOfWeek.SUNDAY) n = n.plusDays(1)
            return n
        }

        fun title(day: LocalDate): String {
            val today = LocalDate.now()
            val prefix = when (day) {
                today -> "Heute"
                today.plusDays(1) -> "Morgen"
                else -> day.format(DateTimeFormatter.ofPattern("EEEE", GERMAN))
            }
            return prefix + " · " + day.format(DateTimeFormatter.ofPattern("EE d. MMM", GERMAN))
        }

        fun encode(day: LocalDate, lessons: List<Lesson>): String {
            fun c(s: String) = s.replace(';', ',').replace('|', '/').replace('\n', ' ').trim()
            val sb = StringBuilder("UNTIS|#").append(title(day))
            for (l in lessons.take(20)) {
                sb.append('\n')
                    .append(hm(l.start)).append(';').append(hm(l.end)).append(';')
                    .append(c(l.subject)).append(';').append(c(l.room)).append(';')
                    .append(c(l.teacher)).append(';').append(l.status).append(';')
                    .append(c(l.info).take(60))
            }
            return sb.toString()
        }
    }
}
