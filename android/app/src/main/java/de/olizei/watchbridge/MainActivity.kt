package de.olizei.watchbridge

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothManager
import android.content.ComponentName
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Bundle
import android.os.PowerManager
import android.provider.Settings
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.AutoAwesome
import androidx.compose.material.icons.filled.Mic
import androidx.compose.material.icons.filled.BatteryFull
import androidx.compose.material.icons.filled.Bluetooth
import androidx.compose.material.icons.filled.CalendarMonth
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.LocationOn
import androidx.compose.material.icons.filled.Notifications
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Visibility
import androidx.compose.material.icons.filled.Watch
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.repeatOnLifecycle
import kotlinx.coroutines.launch

// --- Farben im Stil von iOS ---------------------------------------------------------

private data class Ios(
    val bg: Color, val card: Color, val text: Color, val secondary: Color,
    val separator: Color, val orange: Color, val green: Color, val blue: Color,
    val red: Color, val purple: Color, val pink: Color, val gray: Color,
)

private val LightIos = Ios(
    Color(0xFFF2F2F7), Color.White, Color.Black, Color(0x993C3C43), Color(0x4A3C3C43),
    Color(0xFFFF9500), Color(0xFF34C759), Color(0xFF007AFF), Color(0xFFFF3B30),
    Color(0xFFAF52DE), Color(0xFFFF2D55), Color(0xFF8E8E93),
)
private val DarkIos = Ios(
    Color.Black, Color(0xFF1C1C1E), Color.White, Color(0x99EBEBF5), Color(0x99545458),
    Color(0xFFFF9F0A), Color(0xFF30D158), Color(0xFF0A84FF), Color(0xFFFF453A),
    Color(0xFFBF5AF2), Color(0xFFFF375F), Color(0xFF8E8E93),
)

class MainActivity : ComponentActivity() {

    private val app get() = WatchBridgeApp.instance

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        handleShare(intent)
        setContent { Root() }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handleShare(intent)
    }

    /** Text aus einer anderen App geteilt -> neue Notiz. */
    private fun handleShare(i: Intent?) {
        if (i?.action != Intent.ACTION_SEND || i.type != "text/plain") return
        val text = i.getStringExtra(Intent.EXTRA_TEXT) ?: return
        app.notes.addShared(i.getStringExtra(Intent.EXTRA_SUBJECT), text)
        Toast.makeText(this, "Notiz gespeichert – wird auf der Uhr angezeigt", Toast.LENGTH_SHORT).show()
        i.action = null
    }

    // --- Berechtigungen -----------------------------------------------------------

    fun has(p: String) = ContextCompat.checkSelfPermission(this, p) == PackageManager.PERMISSION_GRANTED

    fun notificationAccess(): Boolean {
        val flat = Settings.Secure.getString(contentResolver, "enabled_notification_listeners") ?: return false
        val me = ComponentName(this, NotifListener::class.java)
        return flat.split(':').any { ComponentName.unflattenFromString(it) == me }
    }

    fun batteryUnrestricted(): Boolean =
        getSystemService(PowerManager::class.java).isIgnoringBatteryOptimizations(packageName)

    @SuppressLint("BatteryLife")
    fun requestBatteryUnrestricted() {
        startActivity(Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS, Uri.parse("package:$packageName")))
    }

    fun openNotificationAccess() {
        startActivity(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS))
    }

    fun openAppSettings() {
        startActivity(Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:$packageName")))
    }

    @SuppressLint("MissingPermission")
    fun bondedDevices(): List<Pair<String, String>> = try {
        getSystemService(BluetoothManager::class.java).adapter.bondedDevices
            .map { (it.name ?: it.address) to it.address }
            .sortedBy { it.first.lowercase() }
    } catch (_: Exception) {
        emptyList()
    }

    // --- Oberfläche -------------------------------------------------------------------

    @Composable
    private fun Root() {
        val dark = isSystemInDarkTheme()
        val c = if (dark) DarkIos else LightIos
        val scheme = if (dark) darkColorScheme(primary = c.orange) else lightColorScheme(primary = c.orange)
        MaterialTheme(colorScheme = scheme) {
            var editing by remember { mutableStateOf<NotesRepo.Note?>(null) }
            Box(
                Modifier
                    .fillMaxSize()
                    .background(c.bg)
                    .windowInsetsPadding(WindowInsets.safeDrawing),
            ) {
                MainList(c) { editing = it }
                editing?.let { n -> NoteEditor(c, n) { editing = null } }
            }
        }
    }

    @Composable
    private fun MainList(c: Ios, onEdit: (NotesRepo.Note) -> Unit) {
        val state by app.link.state.collectAsState()
        val log by app.bridge.log.collectAsState()
        val notes by app.notes.notes.collectAsState()
        val scope = rememberCoroutineScope()

        // Berechtigungen beim Zurückkehren in die App neu prüfen
        var tick by remember { mutableIntStateOf(0) }
        val owner = LocalLifecycleOwner.current
        LaunchedEffect(owner) {
            owner.lifecycle.repeatOnLifecycle(Lifecycle.State.RESUMED) { tick++ }
        }

        val btLauncher = rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) {
            tick++
            if (has(Manifest.permission.BLUETOOTH_CONNECT) && app.prefs.autoStart) BridgeService.start(this@MainActivity)
        }
        val permLauncher = rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { tick++ }
        val bgLocLauncher = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { tick++ }

        if (tick < 0) return  // liest tick -> Neuberechnung nach Rückkehr in die App
        val btOk = has(Manifest.permission.BLUETOOTH_SCAN) && has(Manifest.permission.BLUETOOTH_CONNECT)
        val notifAccess = notificationAccess()
        val postOk = has(Manifest.permission.POST_NOTIFICATIONS)
        val locOk = has(Manifest.permission.ACCESS_FINE_LOCATION)
        val bgLocOk = has(Manifest.permission.ACCESS_BACKGROUND_LOCATION)
        val battOk = batteryUnrestricted()
        val micOk = has(Manifest.permission.RECORD_AUDIO)
        var serviceOn by remember { mutableStateOf(BridgeService.running || app.prefs.autoStart) }

        LazyColumn(
            Modifier
                .fillMaxSize()
                .imePadding(),
            contentPadding = androidx.compose.foundation.layout.PaddingValues(16.dp, 8.dp, 16.dp, 32.dp),
        ) {
            item {
                Text("WatchBridge", color = c.text, fontSize = 34.sp, fontWeight = FontWeight.Bold,
                    modifier = Modifier.padding(start = 4.dp, top = 12.dp, bottom = 12.dp))
                StatusCard(c, state, log, serviceOn)
            }

            // Verbindung
            item { Header(c, "Uhr") }
            item {
                Group(c) {
                    ToggleRow(c, Icons.Default.Watch, c.orange, "Verbindung aktiv", serviceOn) { on ->
                        if (on && !btOk) {
                            btLauncher.launch(arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT))
                        }
                        serviceOn = on
                        app.prefs.autoStart = on
                        if (on && btOk) BridgeService.start(this@MainActivity) else if (!on) BridgeService.stop(this@MainActivity)
                    }
                    Divider(c)
                    var fwd by remember { mutableStateOf(app.prefs.forwardNotifications) }
                    ToggleRow(c, Icons.Default.Notifications, c.red, "Mitteilungen weiterleiten", fwd) {
                        fwd = it
                        app.prefs.forwardNotifications = it
                    }
                    Divider(c)
                    ActionRow(c, Icons.Default.Refresh, c.blue, "Uhr neu suchen",
                        if (app.prefs.watchAddress != null) "Gekoppelt: ${app.prefs.watchAddress}" else "Noch keine Uhr gefunden") {
                        app.link.forgetAndRescan()
                    }
                }
            }

            // Berechtigungen
            val missing = listOf(btOk, notifAccess, postOk, locOk, bgLocOk, battOk, micOk).count { !it }
            item { Header(c, if (missing == 0) "Berechtigungen" else "Berechtigungen · $missing fehlen") }
            item {
                Group(c) {
                    PermRow(c, Icons.Default.Bluetooth, c.blue, "Bluetooth", "Verbindung zur Uhr", btOk) {
                        btLauncher.launch(arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT))
                    }
                    Divider(c)
                    PermRow(c, Icons.Default.Notifications, c.red, "Mitteilungszugriff", "Mitteilungen, Spotify, Navigation", notifAccess) {
                        openNotificationAccess()
                    }
                    Divider(c)
                    PermRow(c, Icons.Default.Notifications, c.gray, "Eigene Mitteilung", "Statusanzeige der Verbindung", postOk) {
                        permLauncher.launch(arrayOf(Manifest.permission.POST_NOTIFICATIONS))
                    }
                    Divider(c)
                    PermRow(c, Icons.Default.LocationOn, c.green, "Standort", "Karte auf der Uhr", locOk) {
                        permLauncher.launch(arrayOf(Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION))
                    }
                    Divider(c)
                    PermRow(c, Icons.Default.LocationOn, c.green, "Standort immer", "Auch bei gesperrtem Handy", bgLocOk) {
                        if (!locOk) {
                            permLauncher.launch(arrayOf(Manifest.permission.ACCESS_FINE_LOCATION))
                        } else {
                            bgLocLauncher.launch(Manifest.permission.ACCESS_BACKGROUND_LOCATION)
                        }
                    }
                    Divider(c)
                    PermRow(c, Icons.Default.BatteryFull, c.orange, "Akku nicht einschränken", "Verbindung bleibt im Hintergrund", battOk) {
                        requestBatteryUnrestricted()
                    }
                    Divider(c)
                    PermRow(c, Icons.Default.Mic, c.pink, "Mikrofon", "Spracherkennung für den KI-Assistenten", micOk) {
                        permLauncher.launch(arrayOf(Manifest.permission.RECORD_AUDIO))
                    }
                }
            }

            // Ray-Ban
            item { Header(c, "Ray-Ban Meta") }
            item { RaybanGroup(c) }

            // Untis
            item { Header(c, "WebUntis") }
            item { UntisGroup(c) }

            // KI-Assistent
            item { Header(c, "KI-Assistent") }
            item { AiGroup(c) }

            // Notizen
            item {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Header(c, "Notizen · ${notes.count { it.onWatch }} auf der Uhr", Modifier.weight(1f))
                    Box(
                        Modifier
                            .padding(top = 18.dp)
                            .size(30.dp)
                            .clip(CircleShape)
                            .background(c.orange)
                            .clickable { onEdit(NotesRepo.Note(title = "", body = "")) },
                        contentAlignment = Alignment.Center,
                    ) { Icon(Icons.Default.Add, "Neue Notiz", tint = Color.White, modifier = Modifier.size(20.dp)) }
                }
            }
            if (notes.isEmpty()) {
                item {
                    Group(c) {
                        Text(
                            "Noch keine Notizen. Tippe auf +, oder teile Text aus Google Keep & Co. an „WatchBridge“.",
                            color = c.secondary, fontSize = 15.sp, modifier = Modifier.padding(16.dp),
                        )
                    }
                }
            } else {
                item {
                    Group(c) {
                        notes.forEachIndexed { i, n ->
                            if (i > 0) Divider(c, inset = 16.dp)
                            NoteRow(c, n, onClick = { onEdit(n) }) { on -> app.notes.setOnWatch(n.id, on) }
                        }
                    }
                }
            }

            item {
                Header(c, "Tasker / MacroDroid")
                Text(
                    "Senden: Broadcast „de.olizei.watchbridge.SEND“ mit Extra msg = \"TOPIC|Inhalt\".\n" +
                        "Empfangen: Intent „de.olizei.watchbridge.WATCH“ (Extras cmd, arg, msg) für Befehle der Uhr.",
                    color = c.secondary, fontSize = 13.sp, modifier = Modifier.padding(horizontal = 16.dp),
                )
                Spacer(Modifier.height(8.dp))
                ActionRowStandalone(c, "Testnachricht an Uhr") {
                    scope.launch {
                        app.bridge.send("NOTIF", "watchbridge-test", "WatchBridge", "Test", "Die Verbindung funktioniert.")
                    }
                }
            }
        }
    }

    @Composable
    private fun StatusCard(c: Ios, state: BleLink.State, log: String, on: Boolean) {
        val (color, title) = when {
            !on -> c.gray to "Aus"
            state == BleLink.State.CONNECTED -> c.green to "Verbunden"
            state == BleLink.State.SCANNING -> c.orange to "Suche Uhr …"
            state == BleLink.State.CONNECTING -> c.orange to "Verbinde …"
            else -> c.red to "Bluetooth aus"
        }
        Group(c) {
            Row(Modifier.padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
                Box(
                    Modifier
                        .size(48.dp)
                        .clip(RoundedCornerShape(12.dp))
                        .background(color),
                    contentAlignment = Alignment.Center,
                ) { Icon(Icons.Default.Watch, null, tint = Color.White, modifier = Modifier.size(28.dp)) }
                Spacer(Modifier.width(14.dp))
                Column {
                    Text("T-Watch S3", color = c.secondary, fontSize = 13.sp)
                    Text(title, color = c.text, fontSize = 20.sp, fontWeight = FontWeight.SemiBold)
                    if (log.isNotEmpty() && state == BleLink.State.CONNECTED) {
                        Text(log, color = c.secondary, fontSize = 13.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    }
                }
            }
        }
    }

    @Composable
    private fun RaybanGroup(c: Ios) {
        val pct by (app.bridge.battery?.raybanPct ?: remember { kotlinx.coroutines.flow.MutableStateFlow(-1) }).collectAsState()
        var showPicker by remember { mutableStateOf(false) }
        var chosen by remember { mutableStateOf(app.prefs.raybanAddress) }
        val devices = remember(showPicker) { if (showPicker) bondedDevices() else emptyList() }
        val name = chosen?.let { a -> bondedDevices().firstOrNull { it.second == a }?.first ?: a } ?: "Automatisch (Name enthält Ray-Ban/Meta)"

        Group(c) {
            ActionRow(c, Icons.Default.Visibility, c.gray, "Gerät", name) { showPicker = true }
            Divider(c)
            InfoRow(c, Icons.Default.BatteryFull, c.green, "Akku", if (pct >= 0) "$pct %" else "Nicht verbunden")
        }
        if (showPicker) {
            AlertDialog(
                onDismissRequest = { showPicker = false },
                confirmButton = { TextButton({ showPicker = false }) { Text("Schließen") } },
                title = { Text("Brille auswählen") },
                text = {
                    Column {
                        PickRow(c, "Automatisch erkennen", chosen == null) {
                            chosen = null; app.prefs.raybanAddress = null; showPicker = false
                            app.bridge.battery?.pollRayban()
                        }
                        devices.forEach { (n, a) ->
                            PickRow(c, n, chosen == a) {
                                chosen = a; app.prefs.raybanAddress = a; showPicker = false
                                app.bridge.battery?.pollRayban()
                            }
                        }
                        if (devices.isEmpty()) Text("Keine gekoppelten Geräte (Bluetooth-Berechtigung?)", color = c.secondary)
                    }
                },
            )
        }
    }

    @Composable
    private fun PickRow(c: Ios, text: String, selected: Boolean, onClick: () -> Unit) {
        Row(
            Modifier
                .fillMaxWidth()
                .clickable(onClick = onClick)
                .padding(vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(text, color = c.text, fontSize = 16.sp, modifier = Modifier.weight(1f))
            if (selected) Text("✓", color = c.orange, fontSize = 18.sp, fontWeight = FontWeight.Bold)
        }
    }

    @Composable
    private fun UntisGroup(c: Ios) {
        val p = app.prefs
        var server by remember { mutableStateOf(p.untisServer) }
        var school by remember { mutableStateOf(p.untisSchool) }
        var user by remember { mutableStateOf(p.untisUser) }
        var pw by remember { mutableStateOf(p.untisPassword) }
        val status by (app.bridge.untis?.status ?: remember { kotlinx.coroutines.flow.MutableStateFlow("Verbindung ist aus") }).collectAsState()
        val scope = rememberCoroutineScope()

        Group(c) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Field("Server (z.B. nessa.webuntis.com)", server) { server = it }
                Field("Schulname (wie im Login)", school) { school = it }
                Field("Benutzer", user) { user = it }
                OutlinedTextField(
                    pw, { pw = it }, label = { Text("Passwort") }, singleLine = true,
                    visualTransformation = PasswordVisualTransformation(),
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password),
                    modifier = Modifier.fillMaxWidth(),
                )
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(Icons.Default.CalendarMonth, null, tint = c.orange, modifier = Modifier.size(18.dp))
                    Spacer(Modifier.width(6.dp))
                    Text(status, color = c.secondary, fontSize = 13.sp, modifier = Modifier.weight(1f))
                    TextButton({
                        p.untisServer = server; p.untisSchool = school; p.untisUser = user; p.untisPassword = pw
                        val u = app.bridge.untis
                        if (u == null) {
                            Toast.makeText(this@MainActivity, "Gespeichert – aktiviere die Verbindung", Toast.LENGTH_SHORT).show()
                        } else {
                            scope.launch { u.refreshNow() }
                        }
                    }) { Text("Speichern & laden") }
                }
            }
        }
    }

    @Composable
    private fun AiGroup(c: Ios) {
        val p = app.prefs
        var key by remember { mutableStateOf(p.aiKey) }
        var cmds by remember { mutableStateOf(p.aiCommands) }
        var question by remember { mutableStateOf("") }
        val status by app.ai.status.collectAsState()

        Group(c) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    RowIcon(Icons.Default.AutoAwesome, c.purple)
                    Spacer(Modifier.width(12.dp))
                    Text("Lange auf ein Watchface drücken und sprechen. Antwort von Claude Opus 5.5, " +
                        "Spracherkennung am Handy.", color = c.secondary, fontSize = 13.sp)
                }
                OutlinedTextField(
                    key, { key = it }, label = { Text("Claude-API-Schlüssel (sk-ant-…)") }, singleLine = true,
                    visualTransformation = PasswordVisualTransformation(),
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password),
                    modifier = Modifier.fillMaxWidth(),
                )
                Text("Schlüssel und Guthaben unter console.anthropic.com – dort am besten ein Monatslimit setzen. " +
                    "Kurze Fragen kosten meist unter 1 Cent.", color = c.secondary, fontSize = 12.sp)
                OutlinedTextField(
                    cmds, { cmds = it }, label = { Text("Eigene Tasker-Befehle (optional)") },
                    placeholder = { Text("licht_an: Wohnzimmerlicht einschalten") },
                    minLines = 2, modifier = Modifier.fillMaxWidth(),
                )
                Text("Ein Befehl pro Zeile. Die KI löst ihn als Intent \"${Bridge.ACTION_FROM_WATCH}\" mit Extra cmd aus.",
                    color = c.secondary, fontSize = 12.sp)
                Row(horizontalArrangement = Arrangement.End, modifier = Modifier.fillMaxWidth()) {
                    TextButton({
                        p.aiKey = key; p.aiCommands = cmds
                        Toast.makeText(this@MainActivity, "Gespeichert", Toast.LENGTH_SHORT).show()
                    }) { Text("Speichern") }
                }
                OutlinedTextField(
                    question, { question = it }, label = { Text("Testfrage tippen") }, singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(status, color = c.secondary, fontSize = 13.sp, modifier = Modifier.weight(1f))
                    TextButton({
                        p.aiKey = key; p.aiCommands = cmds
                        app.ai.askText(question.ifBlank { "Sag kurz Hallo." })
                    }) { Text("Fragen") }
                }
            }
        }
    }

    @Composable
    private fun Field(label: String, value: String, onChange: (String) -> Unit) {
        OutlinedTextField(value, onChange, label = { Text(label) }, singleLine = true, modifier = Modifier.fillMaxWidth())
    }

    @Composable
    private fun NoteRow(c: Ios, n: NotesRepo.Note, onClick: () -> Unit, onToggle: (Boolean) -> Unit) {
        Row(
            Modifier
                .fillMaxWidth()
                .clickable(onClick = onClick)
                .padding(start = 16.dp, end = 12.dp, top = 10.dp, bottom = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(Modifier.weight(1f)) {
                Text(n.title.ifBlank { "Ohne Titel" }, color = c.text, fontSize = 17.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                if (n.body.isNotBlank()) {
                    Text(n.body.replace('\n', ' '), color = c.secondary, fontSize = 14.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                }
            }
            Spacer(Modifier.width(8.dp))
            IosSwitch(c, n.onWatch, onToggle)
        }
    }

    @Composable
    private fun NoteEditor(c: Ios, note: NotesRepo.Note, onClose: () -> Unit) {
        var title by remember { mutableStateOf(note.title) }
        var body by remember { mutableStateOf(note.body) }
        var onWatch by remember { mutableStateOf(note.onWatch) }
        val exists = app.notes.get(note.id) != null
        Column(
            Modifier
                .fillMaxSize()
                .background(c.bg)
                .imePadding()
                .padding(16.dp),
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                TextButton(onClose) { Text("Abbrechen", color = c.orange, fontSize = 17.sp) }
                Spacer(Modifier.weight(1f))
                if (exists) {
                    TextButton({ app.notes.delete(note.id); onClose() }) {
                        Icon(Icons.Default.Delete, "Löschen", tint = c.red)
                    }
                }
                TextButton({
                    if (title.isNotBlank() || body.isNotBlank()) {
                        app.notes.upsert(note.copy(title = title.trim(), body = body.trim(), onWatch = onWatch))
                    }
                    onClose()
                }) { Text("Fertig", color = c.orange, fontSize = 17.sp, fontWeight = FontWeight.SemiBold) }
            }
            Group(c) {
                Row(Modifier.padding(horizontal = 16.dp, vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                    Icon(Icons.Default.Watch, null, tint = c.orange, modifier = Modifier.size(20.dp))
                    Spacer(Modifier.width(10.dp))
                    Text("Auf Uhr anzeigen", color = c.text, fontSize = 17.sp, modifier = Modifier.weight(1f))
                    IosSwitch(c, onWatch) { onWatch = it }
                }
            }
            Spacer(Modifier.height(12.dp))
            OutlinedTextField(title, { title = it }, label = { Text("Titel") }, singleLine = true, modifier = Modifier.fillMaxWidth())
            Spacer(Modifier.height(8.dp))
            OutlinedTextField(body, { body = it }, label = { Text("Notiz") }, modifier = Modifier.fillMaxWidth().weight(1f))
        }
    }

    // --- Bausteine (gruppierte Liste wie in den iOS-Einstellungen) --------------------

    @Composable
    private fun Header(c: Ios, text: String, modifier: Modifier = Modifier) {
        Text(
            text.uppercase(), color = c.secondary, fontSize = 13.sp,
            modifier = modifier.padding(start = 16.dp, top = 24.dp, bottom = 6.dp),
        )
    }

    @Composable
    private fun Group(c: Ios, content: @Composable () -> Unit) {
        Column(
            Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(12.dp))
                .background(c.card),
        ) { content() }
    }

    @Composable
    private fun Divider(c: Ios, inset: androidx.compose.ui.unit.Dp = 58.dp) {
        Box(
            Modifier
                .padding(start = inset)
                .fillMaxWidth()
                .height(0.5.dp)
                .background(c.separator),
        )
    }

    @Composable
    private fun RowIcon(icon: ImageVector, tint: Color) {
        Box(
            Modifier
                .size(30.dp)
                .clip(RoundedCornerShape(7.dp))
                .background(tint),
            contentAlignment = Alignment.Center,
        ) { Icon(icon, null, tint = Color.White, modifier = Modifier.size(18.dp)) }
    }

    @Composable
    private fun RowBase(c: Ios, icon: ImageVector, tint: Color, onClick: (() -> Unit)?, trailing: @Composable () -> Unit, text: @Composable () -> Unit) {
        Row(
            Modifier
                .fillMaxWidth()
                .then(if (onClick != null) Modifier.clickable(onClick = onClick) else Modifier)
                .padding(horizontal = 14.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            RowIcon(icon, tint)
            Spacer(Modifier.width(14.dp))
            Box(Modifier.weight(1f)) { text() }
            trailing()
        }
    }

    @Composable
    private fun TwoLine(c: Ios, title: String, sub: String?) {
        Column {
            Text(title, color = c.text, fontSize = 17.sp)
            if (!sub.isNullOrBlank()) Text(sub, color = c.secondary, fontSize = 13.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
    }

    @Composable
    private fun ToggleRow(c: Ios, icon: ImageVector, tint: Color, title: String, value: Boolean, onChange: (Boolean) -> Unit) {
        RowBase(c, icon, tint, null, { IosSwitch(c, value, onChange) }) { TwoLine(c, title, null) }
    }

    @Composable
    private fun ActionRow(c: Ios, icon: ImageVector, tint: Color, title: String, sub: String?, onClick: () -> Unit) {
        RowBase(c, icon, tint, onClick, {
            Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, null, tint = c.secondary)
        }) { TwoLine(c, title, sub) }
    }

    @Composable
    private fun InfoRow(c: Ios, icon: ImageVector, tint: Color, title: String, value: String) {
        RowBase(c, icon, tint, null, { Text(value, color = c.secondary, fontSize = 17.sp) }) { TwoLine(c, title, null) }
    }

    @Composable
    private fun PermRow(c: Ios, icon: ImageVector, tint: Color, title: String, sub: String, ok: Boolean, onFix: () -> Unit) {
        RowBase(c, icon, tint, if (ok) null else onFix, {
            if (ok) {
                Text("✓", color = c.green, fontSize = 18.sp, fontWeight = FontWeight.Bold)
            } else {
                Box(
                    Modifier
                        .clip(RoundedCornerShape(50))
                        .background(c.orange)
                        .padding(horizontal = 12.dp, vertical = 5.dp),
                ) { Text("Erlauben", color = Color.White, fontSize = 14.sp, fontWeight = FontWeight.SemiBold) }
            }
        }) { TwoLine(c, title, sub) }
    }

    @Composable
    private fun ActionRowStandalone(c: Ios, title: String, onClick: () -> Unit) {
        Group(c) {
            Text(
                title, color = c.orange, fontSize = 17.sp,
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable(onClick = onClick)
                    .padding(16.dp),
            )
        }
    }

    @Composable
    private fun IosSwitch(c: Ios, value: Boolean, onChange: (Boolean) -> Unit) {
        Switch(
            value, onChange,
            colors = SwitchDefaults.colors(
                checkedTrackColor = c.green, checkedThumbColor = Color.White, checkedBorderColor = c.green,
                uncheckedThumbColor = Color.White, uncheckedTrackColor = c.separator, uncheckedBorderColor = Color.Transparent,
            ),
        )
    }
}
