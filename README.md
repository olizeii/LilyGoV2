# T-Watch-S3 · Apple-Style Firmware

Firmware für die LilyGo T-Watch-S3 (ESP32-S3) mit LVGL 8.3, einem 2D-Raster (`lv_tileview`), Light/Dark Mode und einer BLE-Brücke zu Tasker auf Android.

## Raster

```
        X=0          X=1 (Start)    X=2           X=3          X=4
Y=0     Tic Tac Toe  Digital        Stundenplan   Kurzbefehl 1 Fernbedienung
Y=1     Runner       Analog         Spotify       Notizen      Hack
Y=2                  Modern         Mitteilungen  Kurzbefehl 2
Y=3                  Info (Akku)
```

* Wischrichtungen werden automatisch aus den Nachbarn im Raster berechnet.
* Nach jedem Wechsel blenden Seitenpunkte (watchOS-Stil) kurz die Position ein: unten die Spalte, rechts die Reihe.
* Die zuletzt betrachtete Watchface-Reihe (Y=0..2) wird im NVS gespeichert. Beim Booten, beim Aufwachen und beim Zurückwischen aus einer anderen Spalte springt die Uhr dorthin.

## Design

Angelehnt an watchOS/iOS (Apple Human Interface Guidelines):

* **Schrift:** Inter (SF-Pro-ähnlich, mit Umlauten) statt Montserrat. Stufen: Caption 15 · Body 18 · Headline 18 semibold · Title 24 · Large 34 · Time 76 · TimeXL 88.
* **Farben:** Apple-Systemfarben, Dark Mode mit reinem Schwarz (OLED-Look), Light Mode im iOS-Grouped-Stil (`#F2F2F7` + weiße Karten). Sekundärtext erreicht mindestens 4,5:1 Kontrast, im Light Mode wird das „accessible orange“ `#C93400` verwendet.
* **Komponenten:** App-Icons als farbige Kreise, die Info-Kachel als gruppierte Liste mit eingerückten Trennlinien, Toggles in Grün, Touch-Ziele ≥ 44 px.
* Fonts neu erzeugen: `tools/gen_fonts.sh <inter-ttf-ordner>` (benötigt `lv_font_conv`). Neue Icons: Codepoint in `EXTRA` eintragen und in `src/ui/icons.h` definieren.

## Touch-Ausrichtung

Das Display läuft mit `TFT_ROTATION 2`. LovyanGFX dreht den Touch mit, der FT6336 liefert aber bereits passende Koordinaten. Deshalb gleicht `TOUCH_OFFSET_ROTATION 2` die Drehung aus, sonst wären Wischen und Tippen in beiden Achsen gespiegelt (siehe `src/config/pins.h`).

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
  core/settings.cpp/h      Preferences/NVS (schreibt nur bei Änderung)
  core/data_store.cpp/h    Daten vom Handy + Pub/Sub + Protokoll-Parser
  ui/theme.cpp/h           Apple-Palette, Systemfarben, Styles, Icon-Helfer, Light/Dark
  ui/icons.h               Zusätzliche FontAwesome-Icons (UTF-8)
  ui/fonts/                Generierte Inter-Fonts (tools/gen_fonts.sh)
  ui/app.h                 WatchApp-Basisklasse (create/onShow/onHide/onTick)
  ui/ui_core.cpp/h         Tileview, Registrierung, Watchface-Gedächtnis, Seitenpunkte
  apps/apps.cpp            ZENTRALE Registry: welche App liegt auf welcher Kachel
  apps/watchfaces.cpp/h    Spalte X=1: Digital, Analog, Modern, Info + Akku-Overlay
  apps/placeholder_app.*   Platzhalter für noch fehlende Apps
```

### Neue App hinzufügen

1. `src/apps/meine_app.h/.cpp` anlegen, Klasse von `WatchApp` ableiten.
2. Für Daten vom Handy in `create()` `store::subscribe(Topic::X, ...)` aufrufen.
3. In `src/apps/apps.cpp` den Platzhalter durch die neue App ersetzen.
4. Nur Styles aus `theme::` verwenden, dann funktioniert Dark Mode automatisch.

## BLE-Protokoll (Tasker)

Gerätename `T-Watch-S3`. Der Service ist der **Nordic UART Service**, deshalb kannst du alles vorab mit der App „Serial Bluetooth Terminal“ testen.

| Richtung     | Characteristic                          | Eigenschaft     |
|--------------|-----------------------------------------|-----------------|
| Handy → Uhr  | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`  | Write           |
| Uhr → Handy  | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`  | Notify          |

Jede Nachricht ist UTF-8-Text im Format `TOPIC|payload`.

**Lange Nachrichten:** Wenn ein Write mit `~` endet, kommt noch ein weiterer Teil. Erst der Write ohne `~` am Ende schließt die Nachricht ab. Die Uhr fordert eine MTU von 517 an, unter Android ist Chunking deshalb meist unnötig.

### Handy → Uhr

| Nachricht                                   | Bedeutung                                   |
|---------------------------------------------|---------------------------------------------|
| `TIME\|1730650000`                          | Unix-Zeit (UTC), stellt Systemzeit + RTC (Tasker: `%TIMES`) |
| `BAT\|phone\|82`                            | Handy-Akku in %                             |
| `BAT\|rayban\|64`                           | Ray-Ban-Akku in %                           |
| `SPOTIFY\|Titel\|Artist\|1`                 | aktueller Song, `1` = spielt, `0` = Pause   |
| `UNTIS\|08:00-08:45;Mathe;R204;\n08:50-...` | Stundenplan, eine Stunde pro Zeile (`\n`)   |
| `MSG\|WhatsApp\|Anna\|Bin gleich da`        | neue Benachrichtigung (max. 20 werden gespeichert) |
| `MSG_CLEAR`                                 | alle Benachrichtigungen löschen             |
| `NOTES\|Text...`                            | Notizen komplett ersetzen (werden im NVS gespeichert) |
| `GPS\|48.1371,11.5754`                      | Koordinaten für die Info-Kachel             |
| `SHORTCUT\|1\|Licht an`                     | Beschriftung von Shortcut-Widget 1/2        |

### Uhr → Handy

| Nachricht        | Wann                                                         |
|------------------|--------------------------------------------------------------|
| `SYNC\|all`      | sobald das Handy Notifications abonniert hat → Tasker sollte alle Daten oben senden |
| `SPOTIFY\|play` / `pause` / `next` / `prev` | Media-Controller (folgt)          |
| `TASK\|<name>`   | Shortcut-Widgets (folgt)                                     |

## Bauen & Flashen

```bash
pio run -t upload && pio device monitor
```

Falls das Bild auf dem Kopf steht oder der Touch gespiegelt ist, passe `TFT_ROTATION` bzw. `TOUCH_OFFSET_ROTATION` in `src/config/pins.h` an.

## Bekannte Grenzen

* Die eingebauten Montserrat-Fonts enthalten nur ASCII. Umlaute vom Handy werden deshalb vorerst umschrieben (ä → ae). Ein eigener Font mit Latin-1 und großen Ziffern für die Uhrzeit ist der nächste Schritt.
* BLE ist noch ohne Pairing bzw. Bonding, d.h. jedes Gerät in Reichweite kann schreiben.
