package de.olizei.watchbridge

import android.content.Context
import android.content.Intent
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraManager
import android.media.AudioFormat
import android.os.BatteryManager
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.ParcelFileDescriptor
import android.speech.RecognitionListener
import android.speech.RecognizerIntent
import android.speech.SpeechRecognizer
import android.util.Base64
import android.util.Log
import com.anthropic.client.AnthropicClient
import com.anthropic.client.okhttp.AnthropicOkHttpClient
import com.anthropic.core.JsonValue
import com.anthropic.core.http.StreamResponse
import com.anthropic.errors.AnthropicIoException
import com.anthropic.errors.AnthropicServiceException
import com.anthropic.errors.BadRequestException
import com.anthropic.errors.PermissionDeniedException
import com.anthropic.errors.RateLimitException
import com.anthropic.errors.UnauthorizedException
import com.anthropic.helpers.MessageAccumulator
import com.anthropic.models.messages.ContentBlockParam
import com.anthropic.models.messages.MessageCreateParams
import com.anthropic.models.messages.MessageParam
import com.anthropic.models.messages.OutputConfig
import com.anthropic.models.messages.RawMessageStreamEvent
import com.anthropic.models.messages.StopReason
import com.anthropic.models.messages.TextBlockParam
import com.anthropic.models.messages.Tool
import com.anthropic.models.messages.ToolResultBlockParam
import com.anthropic.models.messages.ToolUseBlock
import com.anthropic.models.messages.WebSearchTool20260209
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import java.io.ByteArrayOutputStream
import java.io.OutputStream
import java.time.ZonedDateTime
import java.time.format.DateTimeFormatter
import java.util.Locale
import java.util.concurrent.Executors

/**
 * KI-Assistent der Uhr:
 *  1. Die Uhr streamt Sprache (16 kHz, IMA-ADPCM, siehe firmware src/core/voice.h) -> wird hier
 *     zu PCM decodiert und über eine Pipe in Androids Spracherkennung geschoben (gratis, am Handy).
 *  2. Der erkannte Text geht an Claude (Opus 5.5) mit Werkzeugen für Uhr- und Handy-Aktionen.
 *  3. Die Antwort wird Stück für Stück als AI_TEXT an die Uhr gestreamt.
 */
class AiAssistant(private val app: WatchBridgeApp) {

    private val bridge get() = app.bridge
    private val main = Handler(Looper.getMainLooper())
    private val audioExec = Executors.newSingleThreadExecutor()
    private val lock = Any()

    @Volatile private var session = 0
    private var pipeRead: ParcelFileDescriptor? = null
    private var pipeWrite: OutputStream? = null
    private val pcm = ByteArrayOutputStream()     // gesamte Aufnahme (für einen zweiten Erkennungsversuch)
    private var audioEnded = false
    private var recognizer: SpeechRecognizer? = null
    private var onDevice = false
    private var job: Job? = null
    @Volatile private var stream: StreamResponse<RawMessageStreamEvent>? = null

    private val history = mutableListOf<MessageParam>()
    private var lastTurnAt = 0L
    private var client: AnthropicClient? = null
    private var clientKey = ""

    private val _status = MutableStateFlow("Noch keine Frage gestellt")
    /** Letzter Austausch + Kosten – nur zur Anzeige in der App. */
    val status: StateFlow<String> = _status

    // --- Nachrichten der Uhr (synchron im BLE-Thread: Reihenfolge der Audio-Blöcke!) ---------

    /** true = Nachricht gehört dem Assistenten. */
    fun onWatchMessage(raw: String): Boolean {
        val cmd = raw.substringBefore('|')
        when (cmd) {
            "AUD" -> audio(raw.substring(4))
            "AUD_END" -> endAudio()
            "AI_START" -> start()
            "AI_CANCEL" -> cancel()
            else -> return false
        }
        return true
    }

    private fun start() {
        cancel()
        val s = session
        bridge.send("AI_STATE", "listen")
        if (app.prefs.aiKey.isBlank()) {
            fail(s, "Claude-Schlüssel fehlt (WatchBridge-App)")
            return
        }
        synchronized(lock) {
            pcm.reset()
            audioEnded = false
            openPipe()
        }
        main.post { startRecognizer(s, preferOnDevice = true) }
    }

    private fun audio(b64: String) {
        val bytes = try {
            decodeBlock(Base64.decode(b64, Base64.DEFAULT))
        } catch (e: IllegalArgumentException) {
            return
        }
        synchronized(lock) {
            if (audioEnded || pcm.size() > MAX_PCM) return
            pcm.write(bytes)
            val w = pipeWrite ?: return
            audioExec.execute { writeSafe(w, bytes) }
        }
    }

    private fun endAudio() {
        synchronized(lock) {
            audioEnded = true
            val w = pipeWrite ?: return
            audioExec.execute { closeSafe(w) }
        }
    }

    /** Alles abbrechen (Uhr hat geschlossen oder neue Frage gestartet). */
    fun cancel() {
        session++
        job?.cancel()
        job = null
        try { stream?.close() } catch (_: Exception) {}
        stream = null
        synchronized(lock) {
            audioEnded = true
            closePipe()
        }
        main.post { destroyRecognizer() }
    }

    // --- Audio --------------------------------------------------------------------------------

    private fun openPipe() {
        closePipe()
        val (r, w) = ParcelFileDescriptor.createPipe()
        pipeRead = r
        pipeWrite = ParcelFileDescriptor.AutoCloseOutputStream(w)
    }

    private fun closePipe() {
        pipeWrite?.let { w -> audioExec.execute { closeSafe(w) } }
        pipeWrite = null
        try { pipeRead?.close() } catch (_: Exception) {}
        pipeRead = null
    }

    private fun writeSafe(w: OutputStream, bytes: ByteArray) {
        try { w.write(bytes) } catch (_: Exception) {}  // Erkennung beendet -> Pipe zu
    }

    private fun closeSafe(w: OutputStream) {
        try { w.close() } catch (_: Exception) {}
    }

    // --- Spracherkennung (Android, auf dem Handy) -------------------------------------------

    private fun startRecognizer(s: Int, preferOnDevice: Boolean) {
        if (s != session) return
        val read = synchronized(lock) { pipeRead } ?: return
        val useOnDevice = preferOnDevice && SpeechRecognizer.isOnDeviceRecognitionAvailable(app)
        if (!useOnDevice && !SpeechRecognizer.isRecognitionAvailable(app)) {
            fail(s, "Keine Spracherkennung auf dem Handy")
            return
        }
        destroyRecognizer()
        val rec = if (useOnDevice) SpeechRecognizer.createOnDeviceSpeechRecognizer(app)
        else SpeechRecognizer.createSpeechRecognizer(app)
        recognizer = rec
        onDevice = useOnDevice
        rec.setRecognitionListener(Listener(s))
        val intent = Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH).apply {
            putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM)
            putExtra(RecognizerIntent.EXTRA_LANGUAGE, "de-DE")
            putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, true)
            putExtra(RecognizerIntent.EXTRA_MAX_RESULTS, 1)
            // Audio kommt von der Uhr statt vom Handy-Mikrofon
            putExtra(RecognizerIntent.EXTRA_AUDIO_SOURCE, read)
            putExtra(RecognizerIntent.EXTRA_AUDIO_SOURCE_CHANNEL_COUNT, 1)
            putExtra(RecognizerIntent.EXTRA_AUDIO_SOURCE_ENCODING, AudioFormat.ENCODING_PCM_16BIT)
            putExtra(RecognizerIntent.EXTRA_AUDIO_SOURCE_SAMPLING_RATE, SAMPLE_RATE)
        }
        rec.startListening(intent)
    }

    private fun destroyRecognizer() {
        recognizer?.let {
            try { it.cancel() } catch (_: Exception) {}
            try { it.destroy() } catch (_: Exception) {}
        }
        recognizer = null
    }

    /** On-Device-Erkennung ging nicht (z.B. Sprachpaket fehlt) -> mit der normalen Erkennung nochmal. */
    private fun retryWithDefault(s: Int) {
        synchronized(lock) {
            openPipe()
            val all = pcm.toByteArray()
            val w = pipeWrite ?: return
            audioExec.execute { writeSafe(w, all) }
            if (audioEnded) audioExec.execute { closeSafe(w) }
        }
        startRecognizer(s, preferOnDevice = false)
    }

    private inner class Listener(private val s: Int) : RecognitionListener {
        private var heard = ""

        override fun onPartialResults(b: Bundle) {
            if (s != session) return
            val t = b.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION)?.firstOrNull()?.trim().orEmpty()
            if (t.isNotEmpty() && t != heard) {
                heard = t
                bridge.send("AI_HEARD", t.replace('\n', ' '))
            }
        }

        override fun onEndOfSpeech() {
            if (s == session) bridge.send("AI_STATE", "think")  // Uhr stoppt das Mikrofon
        }

        override fun onResults(b: Bundle) {
            if (s != session) return
            val t = b.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION)?.firstOrNull()?.trim().orEmpty()
                .ifEmpty { heard }
            main.post { destroyRecognizer() }
            synchronized(lock) { audioEnded = true; closePipe() }
            if (t.isBlank()) fail(s, "Nichts verstanden") else ask(s, t)
        }

        override fun onError(error: Int) {
            if (s != session) return
            Log.w(TAG, "Spracherkennung Fehler $error (onDevice=$onDevice)")
            val nothingSaid = error == SpeechRecognizer.ERROR_NO_MATCH || error == SpeechRecognizer.ERROR_SPEECH_TIMEOUT
            if (onDevice && !nothingSaid) {
                retryWithDefault(s)
                return
            }
            if (nothingSaid && heard.isNotBlank()) {  // Teilergebnis reicht
                main.post { destroyRecognizer() }
                ask(s, heard)
                return
            }
            fail(s, when (error) {
                SpeechRecognizer.ERROR_NO_MATCH, SpeechRecognizer.ERROR_SPEECH_TIMEOUT -> "Nichts verstanden"
                SpeechRecognizer.ERROR_INSUFFICIENT_PERMISSIONS -> "Mikrofon in WatchBridge erlauben"
                SpeechRecognizer.ERROR_NETWORK, SpeechRecognizer.ERROR_NETWORK_TIMEOUT -> "Kein Internet"
                SpeechRecognizer.ERROR_LANGUAGE_NOT_SUPPORTED, SpeechRecognizer.ERROR_LANGUAGE_UNAVAILABLE ->
                    "Deutsch-Sprachpaket fehlt"
                else -> "Spracherkennung: Fehler $error"
            })
        }

        override fun onReadyForSpeech(params: Bundle?) {}
        override fun onBeginningOfSpeech() {}
        override fun onRmsChanged(rmsdB: Float) {}
        override fun onBufferReceived(buffer: ByteArray?) {}
        override fun onEvent(eventType: Int, params: Bundle?) {}
    }

    // --- Claude -------------------------------------------------------------------------------

    /** Frage auch ohne Uhr-Mikrofon stellen (Test in der App). */
    fun askText(text: String) {
        cancel()
        ask(session, text)
    }

    private fun ask(s: Int, text: String) {
        if (s != session) return
        bridge.send("AI_HEARD", text.replace('\n', ' '))
        bridge.send("AI_STATE", "think")
        _status.value = "Du: $text\n…"
        job = app.scope.launch(Dispatchers.IO) {
            try {
                runClaude(s, text)
            } catch (e: CancellationException) {
                throw e
            } catch (e: UnauthorizedException) {
                fail(s, "Claude-Schlüssel ungültig")
            } catch (e: PermissionDeniedException) {
                fail(s, "Kein Zugriff auf Claude (Konto prüfen)")
            } catch (e: RateLimitException) {
                fail(s, "Zu viele Anfragen – kurz warten")
            } catch (e: BadRequestException) {
                Log.w(TAG, "Claude 400", e)
                synchronized(history) { history.clear() }
                fail(s, if (e.message?.contains("credit", true) == true) "Kein API-Guthaben mehr" else "Anfrage abgelehnt (400)")
            } catch (e: AnthropicServiceException) {
                Log.w(TAG, "Claude Fehler", e)
                fail(s, "Claude nicht erreichbar (${e.statusCode()})")
            } catch (e: AnthropicIoException) {
                if (s == session) fail(s, "Kein Internet")
            } catch (e: Exception) {
                Log.w(TAG, "Assistent", e)
                if (s == session) fail(s, "Fehler: ${e.javaClass.simpleName}")
            } finally {
                stream = null
            }
        }
    }

    private fun clientFor(key: String): AnthropicClient {
        client?.let { if (clientKey == key) return it }
        return AnthropicOkHttpClient.builder().apiKey(key).build().also {
            client = it
            clientKey = key
        }
    }

    private fun runClaude(s: Int, text: String) {
        val c = clientFor(app.prefs.aiKey)
        val pump = TextPump(s)
        val tools = buildTools()
        var inTok = 0L
        var outTok = 0L

        synchronized(history) {
            val now = System.currentTimeMillis()
            // Neues Gespräch nach 5 min Pause oder wenn es zu lang wird (nur ganze Gespräche verwerfen)
            if (now - lastTurnAt > 5 * 60_000 || history.size > 30) history.clear()
            lastTurnAt = now
            history += userText(context() + "\n\n" + text)
        }

        for (round in 0 until MAX_ROUNDS) {
            if (s != session) return
            val params = MessageCreateParams.builder()
                .model(MODEL)
                .maxTokens(8000L)
                .system(SYSTEM_PROMPT)
                // Kurze Sprachfragen: wenig Nachdenken = schnelle Antwort
                .outputConfig(OutputConfig.builder().effort(OutputConfig.Effort.LOW).build())
                .apply { tools.forEach { addTool(it) } }
                .addTool(WebSearchTool20260209.builder().maxUses(3L).build())
                .messages(synchronized(history) { history.toList() })
                // Lehnt Opus eine Anfrage ab, beantwortet sie Anthropic automatisch mit einem passenden Ersatzmodell
                .putAdditionalHeader("anthropic-beta", "server-side-fallback-2026-07-01")
                .putAdditionalBodyProperty("fallbacks", JsonValue.from("default"))
                .build()

            val acc = MessageAccumulator.create()
            c.messages().createStreaming(params).use { st ->
                stream = st
                st.stream().forEach { ev ->
                    acc.accumulate(ev)
                    if (s == session) ev.contentBlockDelta().flatMap { it.delta().text() }.ifPresent { pump.add(it.text()) }
                }
            }
            stream = null
            val msg = acc.message()
            inTok += msg.usage().inputTokens()
            outTok += msg.usage().outputTokens()
            if (s != session) return
            synchronized(history) { history += msg.toParam() }

            when (msg.stopReason().orElse(null)) {
                StopReason.TOOL_USE -> {
                    pump.boundary()
                    val results = msg.content().mapNotNull { it.toolUse().orElse(null) }.map { runTool(it) }
                    synchronized(history) { history += MessageParam.ofUserOfContentBlockParams(results) }
                }
                StopReason.PAUSE_TURN -> pump.boundary()  // Websuche läuft weiter -> einfach erneut senden
                StopReason.REFUSAL -> {
                    synchronized(history) { history.clear() }
                    fail(s, "Dazu kann ich leider nichts sagen.")
                    return
                }
                else -> {
                    pump.flush()
                    bridge.send("AI_DONE")
                    val cents = (inTok * IN_PRICE + outTok * OUT_PRICE) / 1_000_000.0 * 100
                    _status.value = "Du: $text\nKI: ${pump.total.trim()}\n" +
                        "%,d Token ≈ %.2f Cent".format(Locale.GERMANY, inTok + outTok, cents)
                    return
                }
            }
        }
        pump.flush()
        bridge.send("AI_DONE")
    }

    private fun userText(t: String): MessageParam =
        MessageParam.ofUserOfContentBlockParams(listOf(ContentBlockParam.ofText(TextBlockParam.builder().text(t).build())))

    /** Aktueller Zustand, damit Claude Fragen wie "Wie voll ist mein Akku?" direkt beantworten kann. */
    private fun context(): String {
        val now = ZonedDateTime.now()
        val sb = StringBuilder("[Kontext, automatisch] Jetzt: ")
        sb.append(now.format(DateTimeFormatter.ofPattern("EEEE, d. MMMM yyyy, HH:mm", Locale.GERMAN)))
        sb.append(" (${now.zone.id}).")
        val bm = app.getSystemService(BatteryManager::class.java)
        if (bm != null) {
            sb.append(" Handy-Akku: ${bm.getIntProperty(BatteryManager.BATTERY_PROPERTY_CAPACITY)} %")
            if (bm.isCharging) sb.append(" (lädt)")
            sb.append('.')
        }
        bridge.maps?.lastPlace?.takeIf { it.isNotBlank() }?.let { sb.append(" Ort: $it.") }
        bridge.media?.describe()?.let { sb.append(" Medien: $it.") }
        bridge.untis?.cachedPlan?.takeIf { it.isNotBlank() }?.let {
            sb.append("\nStundenplan (Beginn;Ende;Fach;Raum;Lehrer;Status N=normal C=entfällt S=Vertretung E=Prüfung;Info):\n")
            sb.append(it.take(1500))
        }
        return sb.toString()
    }

    // --- Werkzeuge ----------------------------------------------------------------------------

    private fun schema(vararg props: Pair<String, Map<String, Any>>): Tool.InputSchema.Properties =
        Tool.InputSchema.Properties.builder().apply { props.forEach { (k, v) -> putAdditionalProperty(k, JsonValue.from(v)) } }.build()

    private fun tool(name: String, desc: String, required: List<String>, vararg props: Pair<String, Map<String, Any>>): Tool =
        Tool.builder()
            .name(name)
            .description(desc)
            .inputSchema(Tool.InputSchema.builder().properties(schema(*props)).required(required).build())
            .eagerInputStreaming(true)
            .build()

    private fun enumProp(desc: String, values: List<String>) = mapOf("type" to "string", "enum" to values, "description" to desc)

    private fun commands(): List<Pair<String, String>> = app.prefs.aiCommands.lines()
        .map { it.trim() }.filter { it.isNotEmpty() }
        .map { line -> line.substringBefore(':').trim() to line.substringAfter(':', "").trim() }
        .filter { it.first.isNotEmpty() }

    private fun buildTools(): List<Tool> {
        val list = mutableListOf(
            tool("media_control", "Steuert die laufende Musik/Medien am Handy (Spotify usw.).", listOf("action"),
                "action" to enumProp("Befehl", MEDIA_ACTIONS)),
            tool("tv_remote", "Infrarot-Fernbedienung der Uhr für Fernseher/Beamer (Marke ist auf der Uhr eingestellt). " +
                "ch_up/ch_down sind beim Optoma-Beamer Menü hoch/runter.", listOf("button"),
                "button" to enumProp("Taste", IR_KEYS),
                "times" to mapOf("type" to "integer", "minimum" to 1, "maximum" to 20, "description" to "Wie oft drücken (Standard 1)")),
            tool("watch_settings", "Ändert Einstellungen der Uhr: Dunkelmodus und/oder Helligkeit.", emptyList(),
                "dark_mode" to mapOf("type" to "boolean"),
                "brightness_percent" to mapOf("type" to "integer", "minimum" to 5, "maximum" to 100)),
            tool("open_watch_app", "Öffnet eine Ansicht auf der Uhr (schließt dabei den Assistenten).", listOf("app"),
                "app" to enumProp("watchface, untis = Stundenplan, media = Musik, messages = Mitteilungen, notes = Notizen, " +
                    "map = Karte, remote = TV-Fernbedienung, activity = Schritte, info = Akku/Status, hopturm/tictactoe/runner = Spiele",
                    WATCH_APPS)),
            tool("set_timer", "Stellt einen Timer auf der Uhr; sie vibriert, wenn er abläuft.", listOf("seconds"),
                "seconds" to mapOf("type" to "integer", "minimum" to 1, "maximum" to 86400),
                "title" to mapOf("type" to "string", "description" to "Kurzer Name, z.B. \"Nudeln\"")),
            tool("add_note", "Speichert eine Notiz in WatchBridge und zeigt sie auf der Uhr unter Notizen.", listOf("title", "text"),
                "title" to mapOf("type" to "string"), "text" to mapOf("type" to "string")),
            tool("flashlight", "Schaltet die Taschenlampe des Handys an oder aus.", listOf("on"),
                "on" to mapOf("type" to "boolean")),
            tool("read_notifications", "Liest die aktuellen Mitteilungen am Handy (App, Titel, Text), z.B. für \"Was hat mir X geschrieben?\".",
                emptyList()),
        )
        val cmds = commands()
        if (cmds.isNotEmpty()) {
            list += tool("phone_command",
                "Löst einen vom Nutzer in Tasker/MacroDroid eingerichteten Befehl am Handy aus. Verfügbare Befehle: " +
                    cmds.joinToString("; ") { "${it.first} = ${it.second.ifEmpty { it.first }}" },
                listOf("command"), "command" to enumProp("Befehl", cmds.map { it.first }))
        }
        return list
    }

    private fun runTool(block: ToolUseBlock): ContentBlockParam {
        val (text, isError) = try {
            @Suppress("UNCHECKED_CAST")
            val input = block._input().convert(Map::class.java) as? Map<String, Any?>
                ?: throw IllegalArgumentException("INVALID_JSON")
            execute(block.name(), input)
        } catch (e: Exception) {
            Log.w(TAG, "Werkzeug ${block.name()}", e)
            "Fehler: ${e.message ?: e.javaClass.simpleName}" to true
        }
        Log.d(TAG, "Werkzeug ${block.name()} -> $text")
        return ContentBlockParam.ofToolResult(
            ToolResultBlockParam.builder().toolUseId(block.id()).content(text).isError(isError).build(),
        )
    }

    private fun str(m: Map<String, Any?>, k: String): String? = (m[k] as? String)?.trim()
    private fun int(m: Map<String, Any?>, k: String): Int? = (m[k] as? Number)?.toInt()
    private fun bool(m: Map<String, Any?>, k: String): Boolean? = m[k] as? Boolean

    private fun bad(msg: String) = "Ungültige Eingabe: $msg" to true

    /** @return Ergebnistext für Claude + ob es ein Fehler war */
    private fun execute(name: String, a: Map<String, Any?>): Pair<String, Boolean> {
        val watchOk = app.link.state.value == BleLink.State.CONNECTED
        return when (name) {
            "media_control" -> {
                val act = str(a, "action")?.takeIf { it in MEDIA_ACTIONS } ?: return bad("action")
                val m = bridge.media ?: return "Medien-Steuerung nicht verfügbar (Mitteilungszugriff fehlt)" to true
                m.command(act)
                "OK" to false
            }
            "tv_remote" -> {
                val key = str(a, "button")?.takeIf { it in IR_KEYS } ?: return bad("button")
                if (!watchOk) return "Uhr nicht verbunden" to true
                bridge.send("AI_ACT", "ir", key, (int(a, "times") ?: 1).coerceIn(1, 20).toString())
                "Gesendet" to false
            }
            "watch_settings" -> {
                if (!watchOk) return "Uhr nicht verbunden" to true
                val dark = bool(a, "dark_mode")
                val bright = int(a, "brightness_percent")
                if (dark == null && bright == null) return bad("nichts angegeben")
                dark?.let { bridge.send("AI_ACT", "dark", if (it) "1" else "0") }
                bright?.let { bridge.send("AI_ACT", "bright", it.coerceIn(5, 100).toString()) }
                "OK" to false
            }
            "open_watch_app" -> {
                val target = str(a, "app")?.takeIf { it in WATCH_APPS } ?: return bad("app")
                if (!watchOk) return "Uhr nicht verbunden" to true
                bridge.send("AI_ACT", "open", target)
                "Geöffnet" to false
            }
            "set_timer" -> {
                val sec = int(a, "seconds")?.takeIf { it in 1..86400 } ?: return bad("seconds")
                if (!watchOk) return "Uhr nicht verbunden" to true
                bridge.send("AI_ACT", "timer", sec.toString(), (str(a, "title") ?: "Timer").replace('|', '/').take(40))
                "Timer läuft" to false
            }
            "add_note" -> {
                val title = str(a, "title")?.takeIf { it.isNotEmpty() } ?: return bad("title")
                app.notes.upsert(NotesRepo.Note(title = title.take(60), body = str(a, "text").orEmpty()))
                "Gespeichert" to false
            }
            "flashlight" -> {
                val on = bool(a, "on") ?: return bad("on")
                val cm = app.getSystemService(Context.CAMERA_SERVICE) as CameraManager
                val id = cm.cameraIdList.firstOrNull {
                    cm.getCameraCharacteristics(it).get(CameraCharacteristics.FLASH_INFO_AVAILABLE) == true
                } ?: return "Keine Taschenlampe gefunden" to true
                cm.setTorchMode(id, on)
                "OK" to false
            }
            "read_notifications" -> {
                val l = NotifListener.instance ?: return "Kein Mitteilungszugriff" to true
                val pm = app.packageManager
                val list = (l.activeNotifications ?: emptyArray())
                    .filter { !it.isOngoing && it.packageName != app.packageName }
                    .sortedByDescending { it.postTime }.take(10)
                    .joinToString("\n") { sbn ->
                        val ex = sbn.notification.extras
                        val label = try {
                            pm.getApplicationLabel(pm.getApplicationInfo(sbn.packageName, 0)).toString()
                        } catch (_: Exception) { sbn.packageName }
                        val title = ex.getCharSequence(android.app.Notification.EXTRA_TITLE) ?: ""
                        val body = ex.getCharSequence(android.app.Notification.EXTRA_BIG_TEXT)
                            ?: ex.getCharSequence(android.app.Notification.EXTRA_TEXT) ?: ""
                        "$label | $title | ${body.toString().take(300)}"
                    }
                (list.ifEmpty { "Keine Mitteilungen" }) to false
            }
            "phone_command" -> {
                val cmd = str(a, "command")?.takeIf { c -> commands().any { it.first == c } } ?: return bad("command")
                app.sendBroadcast(Intent(Bridge.ACTION_FROM_WATCH).putExtra("cmd", cmd).putExtra("arg", "")
                    .putExtra("msg", "AI|$cmd"))
                "Ausgelöst" to false
            }
            else -> "Unbekanntes Werkzeug" to true
        }
    }

    // --- Ausgabe an die Uhr -------------------------------------------------------------------

    /** Sammelt Text-Stücke und schickt sie gebündelt (alle ~150 ms) als AI_TEXT. */
    private inner class TextPump(private val s: Int) {
        private val pending = StringBuilder()
        private val all = StringBuilder()
        private var lastFlush = System.currentTimeMillis()
        private var needSpace = false
        val total: String get() = all.toString()

        fun add(t: String) {
            if (t.isEmpty()) return
            if (needSpace && all.isNotEmpty() && !all.last().isWhitespace() && !t.first().isWhitespace()) pending.append(' ')
            needSpace = false
            pending.append(t)
            val now = System.currentTimeMillis()
            if (now - lastFlush > 150 || pending.length > 200) flush()
        }

        /** Zwischen zwei Antwortteilen (vor/nach einem Werkzeug) ein Leerzeichen einfügen. */
        fun boundary() {
            flush()
            needSpace = true
        }

        fun flush() {
            lastFlush = System.currentTimeMillis()
            if (pending.isEmpty() || s != session) return
            val t = pending.toString()
            pending.setLength(0)
            all.append(t)
            bridge.send("AI_TEXT", t)
        }
    }

    private fun fail(s: Int, msg: String) {
        if (s != session) return
        bridge.send("AI_ERR", msg)
        _status.value = "Fehler: $msg"
        synchronized(lock) { audioEnded = true; closePipe() }
        main.post { destroyRecognizer() }
    }

    companion object {
        private const val TAG = "AiAssistant"
        private const val MODEL = "claude-opus-5-5"
        private const val IN_PRICE = 4.0    // $ pro 1 Mio. Token (Opus 5.5)
        private const val OUT_PRICE = 20.0
        private const val MAX_ROUNDS = 6
        private const val SAMPLE_RATE = 16000
        private const val MAX_PCM = 16000 * 2 * 20   // max. 20 s Aufnahme puffern

        private val MEDIA_ACTIONS = listOf("toggle", "play", "pause", "next", "prev", "vol_up", "vol_down")
        private val IR_KEYS = listOf("power", "vol_up", "vol_down", "mute", "ch_up", "ch_down", "input")
        private val WATCH_APPS = listOf("watchface", "untis", "media", "messages", "notes", "map", "remote",
            "activity", "info", "hopturm", "tictactoe", "runner")

        private val SYSTEM_PROMPT = """
            Du bist der Sprachassistent auf der Smartwatch des Nutzers (LilyGo T-Watch, rundes 240×240-Pixel-Display).
            Deine Antworten werden auf diesem winzigen Display gelesen. Antworte deshalb immer auf Deutsch, in ein bis
            drei kurzen Sätzen (höchstens etwa 40 Wörter), als schlichten Fließtext ohne Markdown, Listen, Überschriften
            oder Emojis.
            Die Frage stammt aus einer Spracherkennung und kann Hörfehler enthalten – deute sie sinnvoll.
            Wenn der Nutzer etwas erledigt haben möchte, verwende das passende Werkzeug und bestätige danach knapp, was du
            getan hast. Passt kein Werkzeug, sag das ehrlich, statt etwas zu erfinden. Für aktuelle Informationen
            (Wetter, Nachrichten, Öffnungszeiten, Ergebnisse) nutze die Websuche und nenne nur das Wesentliche.
            Jede Nachricht beginnt mit einem automatisch erzeugten Kontext (Uhrzeit, Akku, Ort, Medien, Stundenplan);
            nutze ihn, wenn er hilft, aber zähle ihn nicht auf.
        """.trimIndent()

        /** Ein ADPCM-Block der Uhr -> PCM 16 Bit little endian. */
        fun decodeBlock(b: ByteArray): ByteArray {
            if (b.size < 5) return ByteArray(0)
            var pred = (b[1].toInt() and 0xFF) or (b[2].toInt() shl 8)   // int16 LE, vorzeichenbehaftet
            var idx = (b[3].toInt() and 0xFF).coerceIn(0, 88)
            val out = ByteArray((b.size - 4) * 4)
            var o = 0
            for (i in 4 until b.size) {
                val byte = b[i].toInt() and 0xFF
                for (nib in intArrayOf(byte and 0x0F, byte shr 4)) {
                    val step = STEP[idx]
                    var vp = step shr 3
                    if (nib and 4 != 0) vp += step
                    if (nib and 2 != 0) vp += step shr 1
                    if (nib and 1 != 0) vp += step shr 2
                    pred = if (nib and 8 != 0) pred - vp else pred + vp
                    pred = pred.coerceIn(-32768, 32767)
                    idx = (idx + IDX[nib]).coerceIn(0, 88)
                    out[o++] = (pred and 0xFF).toByte()
                    out[o++] = (pred shr 8).toByte()
                }
            }
            return out
        }

        private val IDX = intArrayOf(-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8)
        private val STEP = intArrayOf(
            7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
            107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
            876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
            4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
            22385, 24623, 27086, 29794, 32767,
        )
    }
}
