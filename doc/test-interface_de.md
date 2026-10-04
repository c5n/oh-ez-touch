# Steuerschnittstelle des Simulators

Der Simulator lässt sich mit einem Skript steuern: Berührungen werden über einen UDP-Socket gesendet, und der Bildschirminhalt kommt als JSON oder als Framebuffer des Panels zurück. So kann ein Shellskript – oder ein Coding-Agent in einer späteren Sitzung – die Frage „Hat diese Änderung den Einstellungsbildschirm beschädigt?“ beantworten, ohne dass jemand auf ein Fenster schauen muss.

Die Schnittstelle gibt es, weil der Simulator bisher nur von außen *eingerichtet* werden konnte. `OHEZ_ITEM` navigiert zu einer Steuerung, `OHEZ_SETTINGS` öffnet einen Tab und `OHEZ_THEME` wählt ein Aussehen – alles beim Start. Danach gab es keine Möglichkeit, eine Schaltfläche zu drücken oder herauszufinden, was passiert ist. Diese Variablen funktionieren weiterhin und sind nach wie vor der schnellste Weg, um an einer bestimmten Stelle zu beginnen; die Schnittstelle ergänzt die andere Hälfte.

**Simulator und Testgerät.** Ein Wandpanel hat keinen Steuer-Socket – der Code wird aus seiner Firmware herauskompiliert, sofern `CONFIG_OHEZ_TESTIF` nicht anders festlegt; siehe [Auf dem Gerät](#auf-dem-gerät) am Ende.

## Start

Erstelle und starte den Simulator wie üblich; die Schnittstelle wird mit ihm aktiviert.

```bash
. ~/esp/esp-idf/export.sh
idf.py -B build/linux build
OHEZ_OFFLINE=1 ./build/linux/oh-ez-touch.elf &
tools/ohez_ctl.py ping
```

`OHEZ_OFFLINE=1` ist nicht erforderlich, aber fast immer erwünscht: Damit werden Sitemap und Icons aus den einkompilierten Fixtures bereitgestellt. So ist der Bildschirm bei jedem Lauf gleich und hängt nicht davon ab, ob ein openHAB-Server erreichbar ist.

Die Schnittstelle benötigt auch keinen Fensterserver: Mit `SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software` läuft sie ohne grafische Oberfläche, und alle nachfolgenden Befehle antworten weiterhin. Siehe [Ohne grafische Oberfläche ausführen](simulator_de.md#headless-betrieb), warum es zwei Variablen braucht.

Sie lauscht auf **127.0.0.1:8781** – einen Port über der Weboberfläche auf 8780. Die Schnittstelle ist absichtlich nur an Loopback gebunden: Der Kanal hat keine Authentifizierung und kann alles auf dem Panel drücken, daher wird er nicht im Netzwerk angeboten.

| Variable | Wirkung |
| --- | --- |
| `OHEZ_TESTIF=0` | den Socket gar nicht erst öffnen |
| `OHEZ_TESTIF_PORT` | an einem anderen Port lauschen |

Ein bereits verwendeter Port verhindert nur die Schnittstelle, nicht den Lauf: Der Simulator protokolliert `bind to 127.0.0.1:8781 ... -- no control interface` und läuft weiter. In der Praxis kollidieren Ports nicht, weil `port_flash.c` mit `flock` eine Sperre auf dem emulierten NVS-Image einrichtet und ein zweiter Simulator den Start verweigert, bevor er diesen Punkt erreicht.

## Die Befehlszeile

`tools/ohez_ctl.py` ist die Bedienoberfläche. Das Skript benötigt nur Python 3 – weder Pillow noch requests – und liefert einen Exitcode ungleich null zurück, wenn ein Befehl abgelehnt wird oder der Simulator nicht antwortet. Deshalb lässt es sich in einem Shellskript genauso auswerten wie `oh-ez-touch-host-test.elf`.

```bash
tools/ohez_ctl.py ping
tools/ohez_ctl.py wait-page                  # until the openHAB page has loaded
tools/ohez_ctl.py screen                     # pretty-printed JSON
tools/ohez_ctl.py status
tools/ohez_ctl.py tap 160 120
tools/ohez_ctl.py tap-tile 4
tools/ohez_ctl.py tap-label "Hallway Dimmer"
tools/ohez_ctl.py longpress 160 120
tools/ohez_ctl.py swipe right                # a drag; the panel ignores it
tools/ohez_ctl.py shot /tmp/panel.png --scale 2
tools/ohez_ctl.py set theme lcars
tools/ohez_ctl.py calibrate-run              # the whole touchscreen calibration
tools/ohez_ctl.py pin-enter 1234             # type into the open PIN pad, then OK
tools/ohez_ctl.py quit
```

Mit `--host`, `--port` und `--web-port` lassen sich die Zieladressen ändern. `send` übergibt eine Befehlszeile unverändert, sodass ein zur Firmware hinzugefügter Befehl ohne Änderung des Skripts verwendet werden kann:

```bash
tools/ohez_ctl.py send swipe 300 120 20 120 200
```

`tap-label` und `tap-tile` sind die nützlichsten Befehle. Sie lesen den `screen`-Dump, suchen das Rechteck der Kachel und tippen dessen Mitte an. So beschreibt ein Test seine Absicht, statt Pixel anzugeben, die sich bei der nächsten Layoutänderung verschieben.

## Das Protokoll

Ein Request-Datagramm, ein Antwort-Datagramm, Klartext, zwischen den Anfragen wird nichts gespeichert. `nc -u 127.0.0.1 8781` ist ein völlig geeigneter Client.

```
[@<id> ]<command> [args...]     request, at most 512 bytes; the newline is optional
[@<id> ]ok [payload]            it worked
[@<id> ]err <reason>            it did not
```

Das Token `@<id>` ist optional und wird in jeder Antwort zurückgegeben, auch bei Ablehnungen. So kann ein Client Anfragen sicher wiederholen: Die Antwort auf eine abgelaufene Anfrage lässt sich erkennen und verwerfen, statt sie als Antwort auf die nächste Anfrage zu lesen. `ohez_ctl.py` sendet immer eine ID.

Ein Argument mit Leerzeichen kann in doppelte Anführungszeichen gesetzt werden (`set hostname "two words"`). Es gibt keine Escape-Syntax.

### Berührung

Koordinaten sind **Panelpixel: 0..319 mal 0..239 im Querformat, 0..239 mal 0..319 im Hochformat, Ursprung links oben.** Es sind keine Fensterpixel – das SDL-Fenster wird doppelt so groß angezeigt. Was auf dem Bildschirm bei (320, 240) zu liegen scheint, liegt hier also bei (160, 120). Jedes vom `screen`-Dump gemeldete Rechteck verwendet denselben Koordinatenraum, sodass eine Kachel direkt anhand des Dumps angetippt werden kann. Koordinaten außerhalb des gültigen Bereichs werden mit `err range` abgelehnt statt begrenzt, da ein begrenzter Tipp auf etwas Reales treffen und unbemerkt das falsche Widget testen würde.

| Befehl | Wirkung |
| --- | --- |
| `tap <x> <y> [hold_ms]` | drücken, halten (60 ms), loslassen |
| `longpress <x> <y> [ms]` | dasselbe, aber 500 ms halten – länger als LVGLs Schwelle für langes Drücken |
| `swipe <dir> [<x> <y>]` | `left`, `right`, `up` oder `down`, über das Panel hinweg |
| `swipe <x1> <y1> <x2> <y2> [ms]` | eine explizite Ziehbewegung, standardmäßig über 200 ms |
| `press <x> <y>` / `move <x> <y>` / `release` | Grundbefehle für eine Ziehbewegung, die von den vorigen Befehlen nicht abgedeckt wird |

**Das Panel unterstützt keine Wischgesten.** `swipe` sendet weiterhin eine echte Ziehbewegung, aber nichts scrollt, und eine Wischbewegung navigiert nicht und aktiviert auch nichts darunter. Die einzige Ziehbewegung ist das Verschieben des Knopfs eines Schiebereglers: Bei einem Druck auf einen Schieberegler senkt LVGL für diesen Druck die Scrollgrenze von 255 px auf 4 px, sodass der Knopf dem Zeiger folgt, sobald dieser sich um 4 px bewegt hat. Bei jedem anderen Druck bleibt die Grenze bei 255. Mit `press`, einigen `move`-Befehlen und `release` lässt sich ein Knopf ziehen; ein Elementbildschirm sendet den Wert an openHAB, sobald der Zeiger angehoben wird. Ein Elementbildschirm wird über seine Zurück-Leiste (`tap 20 20`) oder mit `settings`/`nav` geschlossen, nicht durch eine Geste.

Das war nicht immer so. Die Vorgeschichte ist wichtig, weil ein Test, der auf dem alten Verhalten basiert, aus dem falschen Grund weiterhin besteht. Auf einer Kachelnavigation gab es nie einen Gesten-Handler: Zurück ging es bei einem Wisch nur im Material-Theme, wo sich die Zurück-Kachel am Anfang einer Wischbewegung befindet und LVGL den Klick an sie auslieferte. In LCARS öffnete dieselbe Bewegung den Einstellungsbildschirm, in JARVIS tat sie nichts – und ein Wisch über die obere Kachelreihe einer Seite schaltete ein Licht ein. Das wurde in `main/ui/ui_input.c` behoben.

`press` lässt den Zeiger unten, bis er angehoben wird. `move` übernimmt seinen aktuellen Zustand; nach einem `press` zieht es also etwas, ohne vorausgegangenen Druck bewegt es sich einfach. `tap`, `longpress` und `swipe` heben den Zeiger zunächst an, wenn er noch unten ist. LVGL beginnt einen Klick bei der Flanke von Loslassen zu Drücken; ein zweiter Druck ohne zwischenzeitliches Loslassen würde daher überhaupt nichts bewirken.

### Lesen

| Befehl | Antwort |
| --- | --- |
| `ping` | `ok <uptime_ms>` |
| `screen` | `ok {json}` – siehe unten |
| `status` | `ok {json}` – Version, Laufzeit, Heap, Framezeit, Netzwerk, openHAB, MQTT |
| `heap` | `ok {json}` – freier Speicher, größter Block, bisheriges Minimum an freiem Speicher, Blockanzahlen |
| `config [<name>]` | `ok {json}` – eine oder alle Einstellungen |
| `calibrate targets` | `ok <x> <y> <x> <y> ...` – Positionen der vier Kreuze |
| `calibrate step` | `ok <pressed> <total>` – Anzahl der erfassten Kreuze |
| `calibrate result` | `ok <was x4> <now x4> <worst_px> <residual_px>` |
| `pin` | `ok {json}` – das unten beschriebene Objekt `pin` aus `screen` |
| `shot` | `ok <url>` – Speicherort des Framebuffers |

### Schreiben

| Befehl | Wirkung |
| --- | --- |
| `set <name> <value>` | eine Einstellung schreiben und speichern sowie Änderungen anwenden, die keinen Neustart erfordern |
| `nav <path>` | einem durch Punkte getrennten Pfad aus Kachelindizes folgen, wie bei `OHEZ_ITEM` |
| `settings [<page>]` | den Einstellungsbildschirm auf einer Seite öffnen oder schließen |
| `calibrate` | Touchscreen-Kalibrierung im geöffneten Einstellungsbildschirm starten |
| `pin <system\|item> set <digits>` | eine PIN speichern, wie auf der Geräteseite |
| `pin <system\|item> clear` | eine PIN entfernen, wie in der Weboberfläche |
| `pin lock` | alle Entsperrungen vergessen, wie beim Inaktivwerden des Panels |
| `quit` | antworten, dann `exit(0)` ausführen – auf einem Gerät gibt es keinen Prozess zum Beenden, daher wird stattdessen neu gestartet und die Antwort kommt nicht mehr an |

`set` nimmt dieselben Namen an, die das Webformular übermittelt (`config` ohne Argument listet alle auf), und verwendet denselben Speicherpfad. Daher **schreibt es eine echte `config.json`**. Weise einen Test mit `OHEZ_CONFIG_DIR` eine eigene Konfiguration zu, so wie `test_config_file.cpp` es tut. Ein unbekannter Wert für eine enumerierte Einstellung wird abgelehnt, statt auf die erste Option zurückzufallen – andernfalls würde ein falsch geschriebener Theme-Name unbemerkt ein anderes Theme auswählen. Eine Texteinstellung wird mit einem Wert aus leeren Anführungszeichen geleert: `set bell_ring ""`; in der Shell lautet der Befehl `tools/ohez_ctl.py send 'set bell_ring ""'`, da `ohez_ctl.py set` einen sichtbaren Wert erwartet.

`nav` verwendet dieselbe Syntax wie `OHEZ_ITEM`: Jeder Schritt außer dem letzten folgt der mit dieser Kachel verknüpften Seite; der letzte öffnet die Steuerung der Kachel. `nav 5` öffnet also die Steuerung der sechsten Kachel, `nav 0.4` folgt der ersten Kachel und öffnet anschließend die fünfte Kachel der dahinterliegenden Seite. Der Befehl antwortet, sobald die Navigation *begonnen* hat – jeder Schritt wartet auf die Seite, die der vorherige angefordert hat. Danach muss `wait-page` folgen.

`settings` verwendet dieselben Namen wie `OHEZ_SETTINGS`: einen Bereich (`wlan`, `openhab`, `mqtt`, `sensors`, `device`, `touch`, `time`, `theme`, `audio`, `info`) oder ein Menü (`settings`, auch als `index` geschrieben, und `system`). Ohne Argument wird der Bildschirm geschlossen, sodass ein Skript einen Besuch einschließen kann.

`settings <page>` öffnet direkt die Seite und **umgeht dabei die System-PIN**: Wie `OHEZ_SETTINGS` ist dies ein programmatischer Einstieg. Ein Skript, das die PIN testen möchte, tippt stattdessen auf **System**. Aus demselben Grund umgeht `nav` die Element-PIN. Für die Eingabe einer PIN gibt es keine Abkürzung. `pin-enter` tippt die Tasten anhand der vom Objekt `pin` gemeldeten Rechtecke an, sodass das Tastenfeld so getestet wird, wie ein Finger es benutzen würde. `pin set` lehnt eine PIN ab, die nicht aus 4 bis 8 Ziffern besteht oder mit der PIN des anderen Geltungsbereichs übereinstimmt.

`calibrate` erfordert, dass der Einstellungsbildschirm bereits geöffnet ist; andernfalls wird der Befehl abgelehnt. Er wird auch auf einem Panel ohne Kalibrierungsfunktion abgelehnt. `calibrate targets` ist besonders nützlich: Der Befehl meldet die Kreuze, **die von der Firmware gezeichnet wurden**, damit der Test diese statt selbst gewählter vier Koordinaten antippt. Würde eine Layoutänderung die Kreuze verschieben, tippte das Skript sonst auf einen leeren Bereich und bestünde aus dem falschen Grund – dieselbe Falle, die `tap-tile` vermeidet, indem es den `screen`-Dump liest.

Auf `calibrate step` wartet ein Skript. Ein Druck kann verloren gehen: Das Overlay wird auf demselben Task erstellt, auf dem der Zeiger gelesen wird, und ein Tipp, der in der falschen Hälfte dieses Vorgangs eintrifft, hat nichts, worauf er wirken kann. Tippe also, prüfe den Zähler und tippe erneut, wenn er sich nicht geändert hat. Eine Wiederholung ist sicher: Die Firmware verwirft einen Druck, der an derselben Stelle gelesen wird wie der zuletzt akzeptierte, da zwei Kreuze, die acht Zehntel eines Bildschirms auseinanderliegen, nicht denselben Messwert erzeugen können. Die Wiederholung ist also entweder der verpasste Druck oder ein folgenloses Duplikat. Stattdessen zu schlafen und auf Erfolg zu hoffen, funktioniert nicht.

`calibrate result` antwortet erst, wenn alle vier Kreuze gedrückt wurden und die Lösung akzeptiert wurde. Die Antwort enthält die vier Konstanten, mit denen das Panel umgerechnet hat, die vier Konstanten, mit denen es umrechnen würde, den größten Fehler der alten Konstanten an den vier Messpunkten und den größten Fehler, der bei den neuen Konstanten verbleibt. Das Ergebnis wird erst gespeichert, wenn **Keep** gedrückt wird.

```bash
tools/ohez_ctl.py settings touch
tools/ohez_ctl.py calibrate
tools/ohez_ctl.py calibrate targets          # tap each pair, then:
tools/ohez_ctl.py calibrate step             # until it reports the next one
tools/ohez_ctl.py calibrate result
```

`tools/ohez_ctl.py calibrate-run` erledigt all das mit einem einzigen Befehl. Das gemessene Panel ist ein Modell; `OHEZ_TOUCH_SKEW` erzeugt den zu ermittelnden Fehler. Siehe [Ein Panel, das nicht existiert](simulator_de.md#ein-panel-das-nicht-existiert).

## Rückgabewert von `screen`

```json
{
  "screen": "item",
  "page": {
    "title": "OhEzTouch Demo",
    "state": "ready",
    "generation": 2,
    "tiles": [
      { "i": 0, "label": "Living Room", "state": "", "type": "group", "pin": true,
        "x": 8, "y": 38, "w": 96, "h": 93 },
      { "i": 5, "label": "Hallway Dimmer", "state": "40.000000", "type": "slider",
        "pin": false, "x": 216, "y": 139, "w": 96, "h": 93 }
    ]
  },
  "item": { "open": true, "type": "slider", "slot": 5 },
  "settings": { "open": false },
  "pin": { "open": false,
           "system": { "set": true, "unlocked": false },
           "item": { "set": true, "unlocked": true } },
  "theme": { "family": "Material", "night": false },
  "backlight": { "brightness": 100, "dimmed": false, "inactive": false,
                 "phase": "awake" }
}
```

`backlight.inactive` wird in dem Moment wahr, in dem das Aktivitäts-Timeout ausgelöst wird. `dimmed` wird wahr, sobald der Dimmwert gilt. Ist der Uhrbildschirm aktiviert, kennzeichnen diese beiden Werte den Beginn und die dunkle Mitte einer Überblendung über Schwarz. `phase` gibt den Schritt an: `awake`, `sleep_out`, `sleep_dark`, `sleep_in`, `asleep`, `wake_out`, `wake_dark` oder `wake_in` (siehe `BacklightControl`). Der Simulator hat keine Hintergrundbeleuchtung, aber die Phasen laufen nach derselben Uhr wie auf dem Gerät.

`screen` bezeichnet den obersten Bildschirm: `page`, `item`, `settings` oder `clock`. Das Objekt `page` beschreibt die darunterliegende openHAB-Kachelnavigation, auch wenn einer der anderen drei Bildschirme darüberliegt; es ist also immer vorhanden.

`banner` erscheint nur, wenn gerade ein Hinweis angezeigt wird, und enthält einen der beiden von der Firmware erzeugten Hinweise: WLAN- und Einrichtungsnachrichten aus `main.cpp` oder „this sitemap will not load“ aus `openhab_ui.cpp`. Es handelt sich um eine `lv_msgbox`: `topic` ist der Titelkopf und `text` der Inhalt. Deshalb sind es zwei Felder und keine einzelne Zeichenkette wie `"topic\ntext"`.

```json
"banner": { "kind": "error", "topic": "SITEMAP ACCESS FAILED",
            "text": "http://...", "hidden": false, "restart": true }
```

`hidden` ist wahr, sobald die Falt-Schaltfläche des Felds gedrückt wurde. Der Hinweis ist weiterhin geöffnet und wird weiterhin gemeldet, denn der einzige Weg zurück zu ihm ist das Hinweis-Symbol, das der Frame anzeigt, solange ein Banner existiert: eine Glocke, ein Warndreieck oder ein Kreuz oben rechts in der Statuszeile aller Theme-Familien außer LCARS. Dort leuchtet stattdessen die unterste Zelle seiner Leiste auf. Durch Tippen auf dieses Symbol werden alle eingeklappten Felder wieder ausgeklappt.

`restart` ist wahr, wenn das Feld in seiner Fußzeile eine **Restart**-Schaltfläche enthält. Das ist bei den beiden Fehlern der Fall, die das Panel nicht selbst beheben kann: „WLAN NOT CONNECTED“ und „SITEMAP ACCESS FAILED“. Dahinter gibt es keine Bestätigung – das Feld selbst ist die Aufforderung. **Tippe nicht per Skript darauf**, es sei denn, ein Neustart des Ziels ist beabsichtigt. Im Simulator beendet der Neustart den Prozess.

**Auf `page.state` sollte gewartet werden.** Das Feld gibt den Ladezyklus der Kacheln an: `idle`, `request`, `waiting`, `ready`. Eine Aktion auf einer Seite, bevor `ready` gemeldet wird, ist der klassische Weg zu einem Test, der auf einem schnellen Rechner besteht und auf einem langsamen scheitert. `tools/ohez_ctl.py wait-page` führt genau diese Warteschleife aus.

`tiles[].type` ist der openHAB-Elementtyp: `group`, `link`, `parent_link`, `number`, `string`, `setpoint`, `slider`, `selection`, `colorpicker`, `switch`, `rollershutter`, `player` oder `image`. Ein `switch` wird durch Antippen direkt umgeschaltet; die meisten anderen Typen öffnen einen Bildschirm, die Link-Typen navigieren. Der Status einer `image`-Kachel enthält den Anfang des von openHAB gespeicherten Base64-Bilds und sagt sonst nichts aus. Der geöffnete Bildbildschirm wird als `item` mit `"type": "image"` gemeldet.

`tiles[].pin` ist wahr, wenn das Element der Kachel das Tag `ohez-pin` trägt. Beim Antippen wird daher zuerst nach der Element-PIN gefragt (schreibgeschützte Kacheln fragen nie danach).

`pin` beschreibt die beiden PINs: ob jede gesetzt und ob sie derzeit entsperrt ist. Ist das Tastenfeld sichtbar, ist `pin.open` wahr, `pin.scope` bezeichnet die abgefragte PIN und `pin.keys` enthält die Rechtecke aller Tasten als `[x, y, w, h]`: `"0"` bis `"9"`, `"b"` für Rückschritt, `"k"` für OK und `"x"` für die Abbrechen-Leiste. Das Tastenfeld liegt über dem durch `screen` bezeichneten Bildschirm, daher ändert sich `screen` selbst nicht.

`config` gibt ein Feld mit `SETTINGS_F_SECRET` – das MQTT-Passwort – als `***` aus. Aus demselben Grund veröffentlicht auch der MQTT-Client es nicht.

## Rückgabewert von `status`

Die meisten Felder erklären sich selbst. Das einzige Objekt, das einen eigenen Absatz verdient, ist `frame`: Es ist der einzige Ort, an dem die Rendergeschwindigkeit des Panels überhaupt gemeldet wird:

```json
"frame": { "valid": true, "age_ms": 240, "window_ms": 3652, "frames": 64,
           "fps": 17.5, "frame_us": 1224, "worst_us": 2665,
           "render_us": 1119, "wait_us": 1,
           "pixels": 20277, "flushes": 2.1 }
```

**`render_us` und `wait_us` sind die Werte, die verglichen werden sollten**, nicht `fps`. Sie teilen einen Frame in die Zeit auf, die LVGLs Software-Renderer fürs Zeichnen benötigt, und die Zeit, die es auf die Annahme des vorherigen Streifens durch das Panel wartet. Anhand dieser Werte lässt sich beurteilen, ob eine Änderung das Panel schneller gemacht oder nur die Kosten verschoben hat. Im Simulator liegt `wait_us` immer ungefähr bei null – der Flush von SDL kehrt nach Abschluss zurück, es gibt also nichts abzuwarten. Auf einem Gerät entspricht der Wert der SPI-Übertragung. Zum Vergleich der Werte dient das [Architekturdokument](architecture_de.md#wofür-die-framezeit-verwendet-wird).

Die Zahlen beschreiben ein abgeschlossenes **Messfenster**, nicht den Zeitpunkt ihres Abrufs. Ein Fenster endet nach 64 Frames oder 10 Sekunden, je nachdem, was zuerst eintritt. Auf einem aktiven Bildschirm wird so eine aktuelle Sekunde des Zeichnens gemeldet; ein ruhiger Bildschirm behält dagegen seinen letzten echten Messwert bei, statt auf null abzufallen. `age_ms` gibt an, wie lange das Ende des Fensters zurückliegt, und `window_ms`, wie lange es dauerte.

**`valid` ist bis zum Ende des ersten Fensters falsch**; bis dahin sind alle übrigen Werte null. Ein Skript, das `fps` ausliest, ohne `valid` zu prüfen, macht eine Assertion über ein Panel, das noch nichts gezeichnet hat. Zuverlässig schließt sich ein Fenster, wenn Zeichenvorgänge ausgelöst werden und man dann wartet: `set theme <name>` zeichnet den gesamten Bildschirm neu und ist das gröbste verfügbare Mittel.

## Bildschirmfotos

```bash
tools/ohez_ctl.py shot /tmp/panel.png            # 320x240
tools/ohez_ctl.py shot /tmp/panel.png --scale 2  # 640x480, as the window shows it
tools/ohez_ctl.py shot /tmp/panel.fb --raw       # the dump, unconverted
```

Das Panel sendet **Rohpixel und niemals ein PNG**. Für die PNG-Kodierung wären ein Ausgabepuffer und der Arbeitsspeicher eines Kompressors nötig. Die Hardware, auf der diese Firmware tatsächlich läuft, hat für beides keinen freien RAM. Die Konvertierung erfolgt daher auf dem Entwicklungsrechner mit etwa dreißig Zeilen `zlib` in `ohez_ctl.py`. Das würde auch einen effizienten Framebuffer-Abruf auf einem Gerät ermöglichen, falls er je implementiert wird; siehe unten.

Die Bytes kommen vom Webserver und nicht vom Steuer-Socket, weil 150 KB nicht in ein Datagramm gehören:

```bash
curl -o /tmp/panel.fb http://127.0.0.1:8780/screenshot.raw   # 153616 bytes
```

Der Body besteht aus einem 16-Byte-Header und anschließend dem Framebuffer, alles Little-Endian:

| Offset | Feld | Aktueller Wert |
| --- | --- | --- |
| 0 | magic | `OHFB` |
| 4 | `u16` version | 1 |
| 6 | `u16` format | 1 = RGB565 |
| 8 | `u16` width | 320 |
| 10 | `u16` height | 240 |
| 12 | `u32` stride | 640, Byte pro Zeile |
| 16 | pixels | stride x height |

Das Ergebnis ist der Framebuffer von LVGL selbst, kein erneutes Rendern des Widget-Baums. Er enthält die Banner auf der obersten Ebene und auch den Zwischenstand einer laufenden Animation. Für die wenigen Millisekunden des Auslesens wird das Neuzeichnen durch die UI angehalten, damit das Bild nicht zerreißt. Wenn ein Client das Lesen abbricht, endet die Sperre nach einer halben Sekunde automatisch.

## Stolperfallen

Diese Punkte können sonst eine Stunde kosten.

- **Tippen vor `page.state == ready` bewirkt nichts.** Beginne ein Skript mit `wait-page` und verwende es nach jeder `nav`-Aktion erneut.
- **Koordinaten liegen im eigenen Raster des Panels – 320x240 im Querformat, 240x320 im Hochformat – und nicht im doppelt so großen Fenster.**
- **Ein Halten unter 20 ms wird abgelehnt**, weil ein so kurzer Druck zwischen zwei LVGL-Eingabeabfragen liegen und vollständig übersehen werden kann. Die Standardeinstellung von 60 ms liegt sicher darüber und deutlich unter den 400 ms, die ein langes Drücken ausmachen.
- **Ein Wisch bewirkt nichts in der UI.** Das ist weder ein Transportfehler noch ein wirkungsloser Befehl auf Übertragungsebene – der Zeiger bewegt sich tatsächlich –, aber es wird nichts geöffnet und nichts gedrückt. Verwende `tap-tile 0`, um eine Seite zurückzugehen, die Zurück-Leiste mit `tap 20 20`, um einen Elementbildschirm zu verlassen, und `nav`, um direkt zu springen. Die beiden folgenden Hinweise zur Ziehbewegung selbst gelten weiterhin.
- **Ein langsamerer Wisch wird *seltener* erkannt als ein schneller.** LVGL berücksichtigt Bewegung für eine Geste nur bei Lesevorgängen mit mindestens 3 px Wegstrecke und benötigt insgesamt mehr als 50 px. Wird ein kurzer Wisch über eine längere Zeit verteilt, fällt er unter die erste Schwelle – das Gegenteil der Intuition. Beide Fälle werden vor dem Einstellen in die Queue geprüft und mit `err gesture too short` oder `err gesture too slow` abgelehnt, statt in der UI stillschweigend zu scheitern.
- **`press` ohne `release` lässt die UI ein Widget gedrückt halten.** Der nächste `tap`-, `longpress`- oder `swipe`-Befehl hebt es zuerst an; kein anderer Befehl tut das.
- **`set` schreibt eine echte `config.json`.** Isoliere Tests mit `OHEZ_CONFIG_DIR`.
- **Das Rechteck einer Kachel kann sich verschieben.** Lies es aus `screen`, statt es festzuschreiben; `tap-label` erledigt das für dich.

## Zusammenspiel

```
main/testif/testif.cpp          the socket and the command table
main/testif/testif_parse.c      the tokeniser -- the one piece with unit tests
main/testif/testif_touch.cpp    the synthetic pointer and its queue
main/testif/testif_report.cpp   the dumps, and `set` / `nav` / `settings`
main/testif/testif_shot.cpp     the framebuffer, and the /screenshot.raw route
tools/ohez_ctl.py               the client, and the PNG writer
```

`testif_loop()` wird von `ohez_loop()` aufgerufen, dem einzigen Task, dem LVGL gehört; der Socket ist nicht blockierend. Ein Befehl wird daher sofort vom einzigen Task ausgeführt, der ihn ausführen darf – es gibt keine Queue und keine Verzögerung. Die Latenz entspricht den 5 ms, die eine Loop-Iteration benötigt. Das umgeht auch die Einschränkung, die `webui_transport_linux.cpp` geprägt hat: Im FreeRTOS-POSIX-Simulator ist nur `select()` umschlossen. Ein blockierender Socket-Aufruf würde den gesamten kooperativen Scheduler und damit den Bildschirm anhalten.

Der simulierte Zeiger ist ein zweites LVGL-Eingabegerät neben der SDL-Maus, sodass das Fenster weiterhin von Hand bedienbar bleibt, während ein Skript es steuert. Seine Queue wird pro *Lesevorgang* und nicht anhand einer Uhr abgearbeitet, was Wischbewegungen zuverlässig macht: LVGL verwirft die angesammelte Distanz für eine Geste bei jedem Lesevorgang ohne Bewegung. Eine von der Echtzeit gesteuerte Planung würde früher oder später dieselbe Position zweimal melden.

## Auf dem Gerät

Die Schnittstelle lässt sich hinter `CONFIG_OHEZ_TESTIF` auch in die Firmware eines Panels einbauen – derselbe Code, dasselbe Protokoll, dieselben Befehle. Die Option ist **standardmäßig ausgeschaltet und sollte auf einem Wandpanel ausgeschaltet bleiben**: Der Kanal hat keine Authentifizierung und kann alles auf dem Bildschirm drücken. In `sdkconfig.defaults.arduitouch_jtag` ist sie eingeschaltet, da ein Testgerät mit angeschlossenem JTAG-Adapter genau der richtige Ort für einen programmierbaren Zeiger ist; der Adapter stellt sicher, dass dieser Build nie an einer Wand landet.

Gegenüber dem Simulator ändern sich Erreichbarkeit und Speicherbudget, nicht die Funktionsweise:

- Der Socket wird an die Netzwerkschnittstelle statt an Loopback gebunden. Die Erreichbarkeit vom Entwicklungsrechner aus ist der ganze Zweck dieser Option; der Preis dafür ist, dass sie standardmäßig deaktiviert bleibt.
- Das Antwortbudget sinkt von 4096 auf 2560 Byte, da der Puffer aus einem Heap stammt, den auch BLE und MQTT verwenden. Die Payload wird direkt in die Antwort geschrieben, nicht erst in einen eigenen zweiten statischen Puffer.
- `quit` startet das Panel neu (es gibt keinen Prozess zum Beenden). Das ist die nützlichere Hälfte des Befehls: Ein Skript, das nach einem Test ein frisches Panel möchte, erhält eines. Der Neustart erfolgt vor dem Senden der Antwort; „keine Antwort“ ist daher die erwartete Antwort.
- `heap` kommt zu den Lese-Befehlen hinzu: freier Speicher, größter Block, bisheriges Minimum an freiem Speicher und Blockanzahlen. Diese Angaben können die Panels im Feld sonst nicht liefern.
- `shot` wird mit „no frame buffer on the device“ abgelehnt: Ein Panel rendert in kleine Zeichenpuffer und hält keinen vollständigen Frame zum Ausliefern vor. Für einen Rücklesevorgang müsste der `flush_cb` des Displays abgegriffen und jeder ausgegebene Bereich mit einem eigenen `{x1,y1,x2,y2}`-Header übertragen werden, damit der Host ihn zusammensetzen kann. Das Rohpixel-Format und die Geometrie pro Bereich sind bereits die passende Form für das Übertragungsformat; es wurde lediglich noch nicht implementiert. Auf einem Gerät ist `screen` die Grundlage für Assertions eines Skripts.

Der simulierte Zeiger ist dort ein zweites Eingabegerät neben dem echten Touchpanel. Beide sind abwechselnd aktiv, so wie die SDL-Maus und der Zeiger im Simulator. Alles andere – `nav`, `settings`, `set`, `screen`, `status` – funktioniert genauso wie hier. Genau das ist der Zweck: Ein Szenario, das auf einem Wandpanel fehlschlägt, lässt sich mit demselben Skript auf dem Testgerät wiederholen.
