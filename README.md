# T-Watch-S3 · Apple-Style Firmware

Firmware für die LilyGo T-Watch-S3 (ESP32-S3) mit LVGL 8.3, einem 2D-Raster (`lv_tileview`) und Light/Dark Mode. Dazu gehört die Android-Begleit-App **WatchBridge**, die Mitteilungen, Spotify, Untis, Notizen, Karte, Navigation und Akkustände auf die Uhr bringt.

## Raster

```
        X=0          X=1 (Start)    X=2           X=3          X=4
Y=0     Hüpfturm     Digital        Stundenplan   Kurzbefehl 1 Fernbedienung
Y=1     Tic Tac Toe  Analog         Spotify       Notizen      Hack
Y=2     Runner       Modern         Mitteilungen  Kurzbefehl 2
Y=3                  Aktivität
Y=4                  Info (Akku, Standort)
```

* Wischrichtungen werden automatisch aus den Nachbarn im Raster berechnet.
* Nach jedem Wechsel blenden Seitenpunkte (watchOS-Stil) kurz die Position ein: unten die Spalte, rechts die Reihe.
* Die zuletzt betrachtete Watchface-Reihe (Y=0..2) wird im NVS gespeichert. Beim Booten, beim Aufwachen und beim Zurückwischen aus einer anderen Spalte springt die Uhr dorthin.

## Funktionen

### Hüpfturm (X=0, Y=0)
Ein grüner Würfel springt in einem senkrechten Schacht Etage für Etage nach oben. Tippen oder die **Seitentaste** lässt ihn springen: senkrecht, mit fester Höhe, ohne Doppelsprung, mit 100 ms Eingabepuffer vor der Landung.
* **Plattformen:** Jede Etage über dem Würfel ist zuerst offen und schließt sich 0,2–0,8 s nach seiner Landung darunter, je höher, desto schneller (siehe Tabelle). Es gibt zwei Typen: **Schiebeplattformen** (Hälften fahren von den Wänden zur Mitte) und **Klappplattformen** (Hälften klappen an Wand-Scharnieren hoch). Eine geschlossene Plattform öffnet sich nie wieder.
* **Schwierigkeit** (Tabelle `LEVELS` in `hop_tower_app.cpp`):

  | Etage | Schließzeit | Flüssigkeit |
  |-------|-------------|-------------|
  | 0–10  | 1,2 s       | 1,0× (15 px/s) |
  | 10–30 | 0,9 s       | 1,3×        |
  | 30–60 | 0,7 s       | 1,6×        |
  | 60+   | 0,5 s       | 2,0×        |
* **Kollision:** Ist die Lücke mindestens so breit wie der Würfel, kommt er durch. Bei 30–100 % wird er **eingeklemmt**, dann drückt ihn schnelles Tippen (≈ 6×) mit einem „Plopp“ heraus, ohne Tippen rutscht er zurück. Unter 30 % prallt er ab.
* **Flüssigkeit:** Sie steigt pink und wellig von unten, wird mit der Höhe schneller und holt bei großem Vorsprung auf (Gummiband). Ist der Würfel eingesperrt, erreicht sie ihn in ~2 s.
* **Squash & Stretch:** Beim Absprung gestreckt, bei der Landung gestaucht mit Nachwabbeln, beim Einklemmen schmal und hoch, bei Game Over zerläuft er.
* **Punkte:** Score = geschaffte Etagen. Etwa jede 10. Etage schwebt ein **Tropfen**, der beim Durchspringen eingesammelt wird. Highscore (`hop_hi`) und Tropfen gesamt (`hop_drops`) bleiben im NVS gespeichert.

### Tic Tac Toe (X=0, Y=1)
Du (✕, blau) spielst gegen die Uhr (◯, orange). Die KI rechnet per Minimax, macht aber zu 25 % einen Zufallszug und ist damit schlagbar. Wer anfängt, wechselt jede Runde. Die Gewinnlinie wird farbig hervorgehoben, dazu gibt es Vibrationsfeedback. Tippen startet die nächste Runde.

### Runner (X=0, Y=2)
Ein Endlos-Laufspiel: Tippen lässt die Figur springen, damit sie den Säulen ausweicht. Das Tempo steigt mit den Punkten, ab 150 Punkten kommen Doppelsäulen. Alle 100 Punkte gibt es einen kurzen Vibrationsimpuls. Der Rekord (🏆) wird im NVS gespeichert. Während des Laufens ist das Wischen gesperrt. Schaltet sich das Display aus oder verlässt du die Kachel, wird das Spiel beendet.

### Schritte / Aktivität (X=1, Y=3 + Watchfaces)
Die Schritte zählt der **BMA423** selbst in Hardware weiter, auch wenn das Display aus ist.
* Die Kachel *Aktivität* zeigt einen Ring wie Apples Bewegungsring (Ziel 10.000), dazu Strecke (Schrittlänge 0,75 m) und Energie (≈ 0,04 kcal/Schritt).
* Digital-Face (Statuszeile) und Analog-Face (bei 6 Uhr) zeigen die Schritte als Komplikation.
* Um Mitternacht beginnt ein neuer Tag. Der Tagesstand wird alle 5 min gesichert, ein Neustart verliert also höchstens wenige Minuten.
* Beim Erreichen des Tagesziels vibriert die Uhr (DRV2605).

### Fernbedienung (X=4, Y=0)
IR-Sender an GPIO 2 (IRremoteESP8266). Oben wählst du die Marke: **Samsung, LG, Sony, Philips (RC6), Panasonic oder Optoma (Beamer)**. Die Wahl wird gespeichert. Bei Optoma steuert die rechte Wippe („NAV“) das Menü hoch/runter, und Power schaltet um: Zum Ausschalten zweimal drücken. Es gibt Tasten für Power (rot), Lauter/Leiser, Programm ± (gedrückt halten wiederholt), Stumm und Quelle. Der IR-Sender hat nur wenige Meter Reichweite, also auf den Fernseher zielen.

### Stundenplan (X=2, Y=0)
Zeigt Karten mit Zeit, Fach, Raum und Lehrkraft. **Entfall** ist rot und durchgestrichen, **Vertretung** orange, **Prüfung** lila. Die laufende Stunde ist mit „JETZT“ markiert, die Liste scrollt automatisch dorthin. Doppelstunden fasst die App zusammen. Nach Schulschluss erscheint der nächste Schultag.

### Spotify / Medien (X=2, Y=1)
Zeigt Titel, Interpret, Fortschritt und die Quell-App. Tasten: zurück, Play/Pause, weiter sowie Lautstärke ±. Das funktioniert mit jeder Medien-App, Spotify wird bevorzugt.

### Mitteilungen (X=2, Y=2)
Die Mitteilungen des Handys erscheinen als Banner mit Vibration und landen in einer Liste mit App-Badge und Alter. In der Detailansicht löscht **Löschen** die Mitteilung auch am Handy. Laufende Spiele unterdrückt das Banner.

### Notizen (X=3, Y=1)
Die Notizen schreibst du in WatchBridge oder teilst sie aus Google Keep und anderen Apps hinein. Pro Notiz legt der Schalter **„Auf Uhr anzeigen“** fest, ob sie auf die Uhr kommt. Die Uhr speichert sie im LittleFS, sie sind also auch ohne Verbindung lesbar.

### Standort, Karte & Navigation (Info-Kachel → Standort)
Die Zeile Standort öffnet eine Vollbild-Karte. Darauf sitzt ein blauer Punkt auf deiner Position, mit Zoom ± (12–18). Das Handy rendert sie aus CARTO-Kacheln (OpenStreetMap-Daten, © OpenStreetMap-Mitwirkende, © CARTO) und überträgt sie als 16-Farben-Bild. Bei Bewegung (> 25 m) wird sie aktualisiert.
Startest du am Handy eine **Google-Maps-Navigation**, liest WatchBridge die Navigations-Mitteilung aus. Die Uhr zeigt dann Pfeil, Entfernung, Anweisung und Ankunft, als Banner und oben in der Karte.

## Design

Angelehnt an watchOS/iOS (Apple Human Interface Guidelines):

* **Schrift:** Inter (SF-Pro-ähnlich, mit Umlauten) statt Montserrat. Stufen: Caption 15 · Body 18 · Headline 18 semibold · Title 24 · Large 34 · Time 76 · TimeXL 88.
* **Farben:** Apple-Systemfarben, Dark Mode mit reinem Schwarz (OLED-Look), Light Mode im iOS-Grouped-Stil (`#F2F2F7` + weiße Karten). Sekundärtext erreicht mindestens 4,5:1 Kontrast, im Light Mode wird das „accessible orange“ `#C93400` verwendet.
* **Komponenten:** App-Icons als farbige Kreise, die Info-Kachel als gruppierte Liste mit eingerückten Trennlinien, Toggles in Grün, Touch-Ziele ≥ 44 px.
* Fonts neu erzeugen: `tools/gen_fonts.sh <inter-ttf-ordner>` (benötigt `lv_font_conv`). Neue Icons: Codepoint in `EXTRA` eintragen und in `src/ui/icons.h` definieren.

## Hochladen

`pio run -t upload --upload-port COM9`. Für den nativen USB-Port der Watch sind in `platformio.ini` zwei Einstellungen nötig: `board_upload.before_reset = usb_reset` und `upload_flags = --no-stub`. Ohne sie bricht der Upload mit „No serial data received“ bzw. „Unable to verify flash chip connection“ ab.

## Touch-Ausrichtung

Das Display läuft mit `TFT_ROTATION 2`. LovyanGFX dreht den Touch mit, der FT6336 liefert aber bereits passende Koordinaten. Deshalb gleicht `TOUCH_OFFSET_ROTATION 2` die Drehung aus, sonst wären Wischen und Tippen in beiden Achsen gespiegelt (siehe `src/config/pins.h`).

## WatchBridge (Android-Begleit-App)

Unter `android/` liegt die Kotlin-App (Jetpack Compose, Android 12+). Sie hält die BLE-Verbindung im Hintergrund und liefert alle Daten:

| Funktion        | Quelle am Handy |
|-----------------|-----------------|
| Uhrzeit         | Systemzeit, auch nach Zeitzonenwechsel |
| Handy-Akku      | `ACTION_BATTERY_CHANGED` (inkl. Laden) |
| Ray-Ban Meta    | Bluetooth-Akkustand des gekoppelten Geräts (automatisch am Namen erkannt oder in der App ausgewählt) |
| Mitteilungen    | NotificationListener (Messenger: letzte Nachricht statt Zusammenfassung) |
| Spotify/Medien  | `MediaSessionManager` + Transport-Controls, Lautstärke über `AudioManager` |
| Untis           | WebUntis-JSON-RPC (Server, Schule, Benutzer, Passwort in der App), alle 30 min |
| Notizen         | In der App / „Teilen“ aus anderen Apps |
| Standort/Karte  | `LocationManager` (Fused), Ortsname per Geocoder, Kartenkacheln von CARTO |
| Navigation      | Mitteilung von Google Maps |

**Einrichten:**
1. Bauen: `cd android && ./gradlew assembleRelease`. Die APK liegt dann unter `android/app/build/outputs/apk/release/app-release.apk`. Am Handy installieren (unbekannte Quellen erlauben).
2. Die App öffnen und **„Verbindung aktiv“** einschalten. Unter *Berechtigungen* alles auf ✓ bringen: Bluetooth, Mitteilungszugriff, Standort („Immer“) und Akku nicht einschränken.
3. Bei *WebUntis* den Server eintragen (z.B. `nessa.webuntis.com`, steht in der Adresszeile von webuntis.com), dazu den Schulnamen wie beim Login, Benutzer und Passwort. Dann **Speichern & laden**.
4. Bei *Ray-Ban Meta* die Brille auswählen, falls sie nicht automatisch erkannt wird.

**Tasker / MacroDroid** (optional):
* Senden: Broadcast-Intent `de.olizei.watchbridge.SEND` mit Extra `msg` = `TOPIC|payload`, z.B. `SHORTCUT|1|Licht an`.
* Empfangen: Befehle der Uhr, die WatchBridge selbst nicht kennt, kommen als Broadcast `de.olizei.watchbridge.WATCH` mit den Extras `cmd`, `arg` und `msg`.

## Projektstruktur

```
include/lv_conf.h          LVGL-Konfiguration (Fonts, Widgets, Tick)
src/
  main.cpp                 Boot-Reihenfolge + Hauptschleife (Sleep/Wake, Ticks)
  ble_manager.cpp/h        NimBLE GATT-Server (Nordic UART Service), thread-sichere Queue
  config/pins.h            Pinbelegung T-Watch-S3, Rotation/Touch-Kalibrierung
  hal/display.cpp/h        LovyanGFX (ST7789 + FT6336) -> LVGL, Backlight, Sleep
  hal/power.cpp/h          AXP2101: Rails, Akku, Seitentaste
  hal/rtc_clock.cpp/h      PCF8563 <-> Systemzeit (UTC in der RTC, TZ Europe/Berlin)
  hal/motion.cpp/h         BMA423 Beschleunigungssensor + Hardware-Schrittzähler
  hal/haptics.cpp/h        DRV2605 Vibration (tap / success / failure)
  core/settings.cpp/h      Preferences/NVS (schreibt nur bei Änderung)
  core/data_store.cpp/h    Daten vom Handy + Pub/Sub + Protokoll-Parser (v2), Notizen im LittleFS
  core/activity.cpp/h      Tages-Schritte, Mitternachts-Reset, NVS-Sicherung, Ziel
  ui/theme.cpp/h           Apple-Palette, Systemfarben, Styles, Icon-Helfer, Light/Dark
  ui/widgets.cpp/h         Gemeinsame Bausteine: Sheets, runde Tasten, Leerzustände
  ui/banner.cpp/h          Mitteilungs- und Navigationsbanner (weckt das Display)
  ui/map_screen.cpp/h      Vollbild-Karte + Navigationsanzeige
  ui/icons.h               Zusätzliche FontAwesome-Icons (UTF-8)
  ui/fonts/                Generierte Inter-Fonts (tools/gen_fonts.sh)
  ui/app.h                 WatchApp-Basisklasse (create/onShow/onHide/onTick)
  ui/ui_core.cpp/h         Tileview, Registrierung, Watchface-Gedächtnis, Seitenpunkte
  apps/apps.cpp            ZENTRALE Registry: welche App liegt auf welcher Kachel
  apps/watchfaces.cpp/h    Spalte X=1: Digital, Analog, Modern, Aktivität, Info + Akku-Overlay
  apps/tictactoe_app.*     Tic Tac Toe gegen Minimax-KI
  apps/hop_tower_app.*     Hüpfturm (vertikaler Sprung-Kletterer)
  apps/runner_app.*        Endlos-Runner (50 fps, Rekord im NVS)
  apps/ir_remote_app.*     TV-/Beamer-Fernbedienung (6 Marken)
  apps/untis_app.*         Stundenplan
  apps/media_app.*         Spotify / Medien-Steuerung
  apps/messages_app.*      Mitteilungen + Detailansicht
  apps/notes_app.*         Notizen
  apps/placeholder_app.*   Platzhalter für noch fehlende Apps
android/                   WatchBridge (Kotlin, Compose)
  app/src/main/java/de/olizei/watchbridge/
    BleLink.kt             BLE-Client: Scan, MTU 517, Write-Queue, Chunking, Auto-Reconnect
    Bridge.kt              Protokoll, Mitteilungen, Google-Maps-Navigation
    BridgeService.kt       Vordergrund-Dienst, startet alle Quellen
    MediaWatcher.kt        Medien-Sessions (Spotify)
    BatteryWatcher.kt      Handy- + Ray-Ban-Akku
    UntisSync.kt           WebUntis-Client
    MapProvider.kt         Standort + Kartenrenderer (LVGL INDEXED_4BIT)
    NotesRepo.kt           Notizen (JSON)
    MainActivity.kt        Einstellungen im iOS-Stil
```

### Neue App hinzufügen

1. `src/apps/meine_app.h/.cpp` anlegen, Klasse von `WatchApp` ableiten.
2. Für Daten vom Handy in `create()` `store::subscribe(Topic::X, ...)` aufrufen.
3. In `src/apps/apps.cpp` den Platzhalter durch die neue App ersetzen.
4. Nur Styles aus `theme::` verwenden, dann funktioniert Dark Mode automatisch.

## BLE-Protokoll (v2)

Gerätename `T-Watch-S3`. Der Service ist der **Nordic UART Service**, deshalb kannst du alles vorab mit der App „Serial Bluetooth Terminal“ testen.

| Richtung     | Characteristic                          | Eigenschaft     |
|--------------|-----------------------------------------|-----------------|
| Handy → Uhr  | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`  | Write           |
| Uhr → Handy  | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`  | Notify          |

Jede Nachricht ist UTF-8-Text im Format `TOPIC|feld|feld|...`, höchstens 8 KB.

**Lange Nachrichten:** Wenn ein Write mit `~` endet, kommt noch ein weiterer Teil. Erst der Write ohne `~` am Ende schließt die Nachricht ab. Endet eine Nachricht selbst auf `~`, hängt der Absender ein Leerzeichen an.

### Handy → Uhr

| Nachricht | Bedeutung |
|-----------|-----------|
| `TIME\|1730650000` | Unix-Zeit (UTC), stellt Systemzeit + RTC |
| `BAT\|phone\|82\|1` | Handy-Akku in %, `1` = lädt |
| `BAT\|rayban\|64` | Ray-Ban-Akku in %, `-1` = nicht verbunden |
| `MEDIA\|Titel\|Artist\|1\|pos\|dauer\|App` | Medien: `1` = spielt, Position und Dauer in s |
| `MEDIA_OFF` | Keine Medien-Session |
| `UNTIS\|#Titel\n08:00;08:45;Mathe;R204;MUE;N;Info` | Stundenplan, eine Stunde pro Zeile. Status: `N` normal, `C` Entfall, `S` Vertretung, `E` Prüfung |
| `NOTIF\|key\|App\|Titel\|Text` | Neue Mitteilung (Banner + Vibration). `NOTIF_S` = still nachgeliefert |
| `NOTIF_DEL\|key` | Mitteilung wurde am Handy entfernt |
| `MSG\|App\|Titel\|Text` / `MSG_CLEAR` | Alt-Format ohne Key / alle löschen |
| `NOTES_BEGIN`, `NOTE\|id\|Titel\|Text`, `NOTES_END` | Notizen komplett ersetzen |
| `GPS\|48.13710,11.57540\|Marienplatz, München` | Position + Ortsname |
| `NAV\|left\|300 m\|Links abbiegen auf …\|Ankunft 14:32` | Navigation. Manöver: `left right slight_left slight_right sharp_left sharp_right uturn roundabout straight arrive depart` |
| `NAV_END` | Navigation beendet |
| `MAP_BEGIN\|240\|240\|28864`, `MAP\|<base64>`, `MAP_END` | Kartenbild (LVGL `INDEXED_4BIT`: 16×BGRA-Palette, dann 4 Bit/Pixel). Base64 in Stücken mit einer Länge, die durch 4 teilbar ist |
| `SHORTCUT\|1\|Licht an` | Beschriftung von Shortcut-Widget 1/2 |

### Uhr → Handy

| Nachricht | Wann |
|-----------|------|
| `SYNC\|all` | Sobald das Handy Notifications abonniert hat. Das Handy schickt dann alles neu |
| `MEDIA\|toggle` / `next` / `prev` / `vol_up` / `vol_down` | Medien-Tasten |
| `UNTIS_REQ` | Stundenplan geöffnet (das Handy lädt höchstens alle 5 min neu) |
| `MAP_REQ\|16\|1` | Karte offen (Zoom, `1` = dunkel), wird alle 20 s erneuert |
| `MAP_STOP` | Karte geschlossen |
| `NOTIF_DISMISS\|key` | Mitteilung auf der Uhr gelöscht → auch am Handy entfernen |

## Bekannte Grenzen

* BLE läuft noch ohne Pairing bzw. Bonding, d.h. jedes Gerät in Reichweite kann schreiben.
* Die Navigation hängt vom Aufbau der Google-Maps-Mitteilung ab. Das Manöver wird aus dem Anweisungstext erraten (Deutsch/Englisch).
* Den Ray-Ban-Akku meldet Android nur, wenn die Brille den Bluetooth-Akkustand überträgt (HFP). Sonst steht dort „Nicht verbunden“.
