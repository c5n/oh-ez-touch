# MQTT

Der Client veröffentlicht, was das Bedienfeld über sich selbst weiß. Er abonniert:

- ein Wildcard-Topic, über das jede Einstellung geschrieben werden kann,
- ein Topic, das einen Klang abspielt,
- und -- auf einem Board, das über sie verfügt -- Topics zur Steuerung der Relais und LEDs.

MQTT ist standardmäßig deaktiviert. Die Einstellungen befinden sich auf der `MQTT`-Seite des Einstellungsbildschirms und in der Weboberfläche.

Jedes Topic beginnt mit `<base topic>/<hostname>`. Mit den Standardwerten ist das `oheztouch/oheztouch-new`. Der Hostname ist Teil des Präfixes, damit ein zweites neues Bedienfeld nicht über das erste veröffentlicht.

> **WARNUNG:** Die MQTT-Schnittstelle ist nicht authentifiziert. Dasselbe gilt für die Weboberfläche. Verwenden Sie beide nur in vertrauenswürdigen Netzwerken.

## Veröffentlichte Topics

| Topic | Veröffentlichungszeitpunkt | Wert |
| --- | --- | --- |
| `status` | bei Verbindung | `online`, und `offline` als Last Will |
| `system/name` | bei Verbindung | Der Hostname |
| `system/target` | bei Verbindung | Für welches Board diese Firmware vorgesehen ist, z. B. `ArduiTouch` |
| `system/version` | bei Verbindung | z. B. `0.20` |
| `system/build` | bei Verbindung | Compiler-Datum und -Uhrzeit |
| `system/git` | bei Verbindung | Der Commit, aus dem diese Firmware erstellt wurde |
| `system/uptime` | bei jedem Intervall | Sekunden seit dem Start |
| `system/heap` | bei jedem Intervall | Freier Heap in Byte |
| `system/heapblock` | bei jedem Intervall | Größter einzelner freier Speicherblock in Byte. Fehlt, wenn das Ziel diesen Wert nicht ermitteln kann. Ein Wert deutlich unter `system/heap` weist auf einen fragmentierten Heap hin. |
| `system/fps` | bei jedem Intervall | Bilder pro Sekunde, mit einer Dezimalstelle. Fehlt, bis der Bildschirm gezeichnet wurde. |
| `system/render_us` | bei jedem Intervall | Mikrosekunden, die der Software-Renderer für ein Bild benötigt |
| `system/wait_us` | bei jedem Intervall | Mikrosekunden, die ein Bild auf das Panel wartet. Siehe [Wofür die Framezeit verwendet wird](architecture_de.md#wofür-die-framezeit-verwendet-wird). |
| `system/ip` | bei jedem Intervall | Die Adresse der Station |
| `system/ssid` | bei jedem Intervall | Das Netzwerk oder der Schnittstellenname auf dem Simulator |
| `system/bssid` | bei jedem Intervall | Die MAC-Adresse des Zugangspunkts oder, wenn kein Funkmodul vorhanden ist, die eigene |
| `system/rssi` | bei jedem Intervall | dBm. Fehlt, wenn kein Funkmodul vorhanden ist. |
| `system/quality` | bei jedem Intervall | Derselbe Wert als Prozentangabe, auf der Skala der Statusleiste |
| `ui/night` | bei jedem Intervall | `ON`, solange die Nachtvariante aktiv ist |
| `ui/backlight` | bei Änderung und bei jedem Intervall | `ON`, wenn das Display aktiviert ist, `OFF`, wenn es gedimmt ist |
| `ui/brightness` | bei Änderung und bei jedem Intervall | `0` bis `100`, der aktuell geltende Wert |
| `ui/activity` | bei Änderung und bei jedem Intervall | `ON`, solange jemand das Bedienfeld benutzt. Siehe [Hintergrundbeleuchtung und Interaktion](#hintergrundbeleuchtung-und-interaktion). |
| `sensor/temperature` | bei jeder Messung | Grad Celsius |
| `sensor/humidity` | bei jeder Messung | Prozent relative Luftfeuchtigkeit |
| `sensor/pressure` | bei jeder Messung | hPa |
| `config/<setting>` | bei Verbindung und nach jedem Speichern | Ein Topic pro Einstellung |
| `relay/<n>` | bei Änderung und bei Verbindung | `ON` oder `OFF`. Nur auf Boards mit Relais. |
| `led/<name>` | bei Änderung und bei Verbindung | `0` bis `100`. Nur auf Boards mit LEDs. |
| `ble/...` | bei Erkennung und in jedem Scanfenster | Eine Gruppe Topics pro BLE-Sender in Reichweite. Siehe [Bluetooth-LE-Beacons](ble_de.md). Erfordert, dass **Nach BLE-Beacons suchen** aktiviert ist. |

Alles wird mit QoS 0 veröffentlicht. Alles wird beibehalten, sofern **Veröffentlichte Werte beibehalten** nicht deaktiviert ist. Jedes Topic enthält den aktuellen Wert eines Zustands. Ein Subscriber, der eine Aktualisierung verpasst hat, benötigt den neuesten Wert, nicht den verpassten. Dafür dient Retain.

Für die Sensortopics muss **BME280-Sensor verwenden** aktiviert sein. Nur dorthin werden die Messwerte gesendet. Die Relais- und LED-Topics setzen die entsprechende Hardware voraus. Siehe [Relais und LEDs](#relais-und-leds).

## Hintergrundbeleuchtung und Interaktion

Drei Topics beschreiben, was das Bedienfeld selbst tut, im Gegensatz zu dem, was es anzeigt.

`ui/backlight` und `ui/brightness` folgen dem Dimm-Zeitlimit. Bei einer Berührung wird das Bedienfeld auf **Normale Helligkeit** aufgehellt und nach **Inaktivitätszeitlimit** Sekunden auf **Gedimmte Helligkeit** abgedunkelt. `ui/brightness` gibt den angesteuerten Wert an, keinen Messwert am Pin: Das Ausblenden wird von der Hardware schrittweise ausgeführt, und dies ist der Wert, auf den es sich zubewegt.

Bei aktiviertem Uhrbildschirm verlaufen beide Richtungen über Schwarz. `ui/brightness` meldet dann während des Dimmens oder Aufweckens in der Mitte `0`. `ui/backlight` wechselt erst dann zu `OFF`, wenn das Bedienfeld dunkel ist und der Uhrbildschirm erscheint, nicht wenn das Ausblenden beginnt. Es wechselt wieder zu `ON`, wenn der Uhrbildschirm 0,1 s nach der Berührung verschwindet.

`ui/activity` ist ein Zeitfenster, kein Ereignis. Es öffnet sich bei der ersten Berührung und schließt zehn Sekunden nach der letzten:

```
touch  touch touch                    touch
  |      |     |                        |
  ON ---------------------------------------------- OFF
                                             10 s ->
```

Ein Finger, der eine Einstellungsliste durchgeht, erzeugt also ein einziges `ON` und nicht dreißig. Dieses Topic kann eine Regel abonnieren, die wissen soll, ob jemand am Bedienfeld steht -- etwa um das Flurlicht einzuschalten, eine Diashow anzuhalten oder eine Gegensprechanlage freizuschalten. Die zehn Sekunden verhindern, dass eine Pause zwischen zwei Berührungen die Aktion beendet.

Eine Berührung ist eine Berührung am Bedienfeld: der Touch-Controller eines Geräts, die Maus oder die Steueroberfläche im Simulator. Die Weboberfläche und MQTT selbst zählen nicht als Interaktion, da sich dort niemand vor Ort befindet.

Beide Topics werden sofort bei einer Änderung statt erst zum Veröffentlichungsintervall veröffentlicht. Es hilft nichts, wenn eine Hintergrundbeleuchtung, die sich einschaltet, sobald jemand zum Bedienfeld kommt, erst eine Minute später gemeldet wird. Das Intervall wiederholt sie trotzdem wie alle anderen Zeilen in der Tabelle oben, damit auch die beibehaltenen Werte aktuell bleiben.

Keines der beiden Topics kann beschrieben werden. Die Helligkeits-*stufen* sind Einstellungen und werden über `config/bl_normal/set` und `config/bl_dim/set` geschrieben, ebenso das Zeitlimit über `config/bl_timeout/set`.

## Eine Einstellung schreiben

`config/<setting>/set` schreibt die Einstellung und speichert sie. Die Namen von `<setting>` sind die POST-Argumentnamen in `main/config/config_fields.cpp`: `theme`, `night_mode`, `bl_normal`, `oh_host` und so weiter. Es ist dieselbe Liste, die das Webformular sendet.

```bash
mosquitto_pub -t oheztouch/oheztouch-new/config/theme/set -m LCARS
mosquitto_pub -t oheztouch/oheztouch-new/config/night_mode/set -m auto
mosquitto_pub -t oheztouch/oheztouch-new/config/bl_dim/set -m 20
```

Eine Farbeinstellung (`clock_day_fg`, `clock_day_bg`, `clock_night_fg`, `clock_night_bg`) wird als `#rrggbb` veröffentlicht und geschrieben. Alle anderen Werte werden verworfen.

```bash
mosquitto_pub -t oheztouch/oheztouch-new/config/clock_night_fg/set -m '#ffb000'
```

Die Wertebereiche, Zeichenregeln und Optionsnamen entsprechen denen, die der Einstellungsbildschirm und das Webformular durchsetzen. Eine Zahl außerhalb ihres Bereichs wird auf den nächstgelegenen Grenzwert begrenzt. Ein Hostname mit `/` oder `:` wird verworfen. Ein unbekannter Optionsname wählt die erste Option. Für ein Kontrollkästchen gelten `ON`, `true`, `yes` oder `1`. Alles andere bedeutet aus.

Zwei Ausnahmen:

- Das Broker-Passwort wird niemals veröffentlicht und kann auf diese Weise nicht gesetzt werden.
- Eine mit `*` markierte Einstellung (nur beim Start gelesen) wird gespeichert, aber erst nach einem Neustart wirksam.

Wenn sich der Wert nicht geändert hat, wird nichts in den Flash-Speicher geschrieben. Es entstehen keine Kosten, wenn ein Broker bei jeder erneuten Verbindung einen beibehaltenen Befehl erneut sendet.

## Einen Klang abspielen

`sound/set` spielt einen Klang des aktiven Themes ab. Der Payload ist der Name des Klangs. Die achtzehn Namen stammen aus dem Klangverzeichnis in `main/ui/ui_beep.hpp`:

```
press        tick         tick_back    toggle_on    toggle_off   change
accept       cancel       link         link_back    screen       screen_out
notify       warning      error        boot         wake         door_chime
```

```bash
mosquitto_pub -t oheztouch/oheztouch-new/sound/set -m door_chime
mosquitto_pub -t oheztouch/oheztouch-new/sound/set -m notify
mosquitto_pub -t oheztouch/oheztouch-new/sound/set -m error
```

Der Name wird ohne Beachtung der Groß- und Kleinschreibung abgeglichen. Ein leerer Payload spielt nichts ab. Ein anderer unbekannter Name wird protokolliert und ignoriert. Wie ein Name klingt, hängt vom Theme ab. `notify` auf einem Material-Bedienfeld und `notify` auf einem LCARS-Bedienfeld sind zwei unterschiedliche Klänge.

`door_chime` ist der einzige Klang, den das Bedienfeld nicht selbst abspielt. Keine Geste auf einem Touchscreen bedeutet „Jemand ist an der Tür“. Verbinden Sie das Topic mit einer Türklingel, und das Bedienfeld antwortet mit dem Klang des aktiven Themes.

Zwei Dinge sind zu beachten:

- **Dieses Topic ist ein Ereignis, kein Zustand.** Eine *beibehaltene* `sound/set`-Nachricht wird verworfen. Alles andere wird vom Broker absichtlich nach einer erneuten Verbindung wiedergegeben. Ein Bedienfeld, das bei jedem Neustart des Brokers piept, wird jemand den Stecker ziehen.
- **Die Einstellungen des Signalgebers gelten auch hier.** Ein ausgeschalteter Signalgeber oder eine Lautstärke von null bleibt stumm.

## Relais und LEDs

Der [Lanbon L8](hardware/lanbon_de.md) hat drei Netzspannungsrelais und ein RGB-Stimmungslicht. Das [Cheap Yellow Display](hardware/cyd_de.md) hat die grünen und blauen Kanäle seiner RGB-LED. Beide werden ausschließlich über MQTT angesteuert. Es gibt keine Einstellung, kein Widget auf dem Bildschirm und kein openHAB-Element. Ein Relais reagiert auf ein Topic. Das ist die gesamte Schnittstelle.

Auf einem Board ohne entsprechende Hardware erscheinen diese Topics nicht. Die ArduiTouch-Boards veröffentlichen und abonnieren davon nichts.

| Topic | Richtung | Wert |
| --- | --- | --- |
| `relay/<n>/set` | eingehend | `ON`, `OFF` oder `TOGGLE` |
| `relay/<n>` | ausgehend | `ON` oder `OFF`, der aktuelle Zustand |
| `led/<name>/set` | eingehend | `0` bis `100` oder `ON` / `OFF` |
| `led/<name>` | ausgehend | `0` bis `100`, die aktuelle Helligkeit |

Die Relais sind wie auf der Wandplatte ab 1 nummeriert: `relay/1` bis `relay/3` auf einem L8-HS. Die LEDs haben die Namen `led/red`, `led/green` und `led/blue`. Es handelt sich um drei unabhängige Helligkeitsstufen und nicht um eine einzelne Farbe. Was daraus gemischt wird, entscheidet der jeweilige Sender.

```bash
mosquitto_pub -t oheztouch/oheztouch-new/relay/1/set -m ON
mosquitto_pub -t oheztouch/oheztouch-new/relay/2/set -m TOGGLE
mosquitto_pub -t oheztouch/oheztouch-new/led/red/set -m 100
mosquitto_pub -t oheztouch/oheztouch-new/led/green/set -m 40
```

Ein Payload für `relay/<n>/set` ist `ON`, `OFF`, `TRUE`, `FALSE`, `YES`, `NO`, `1` oder `0`, ohne Beachtung der Groß- und Kleinschreibung, sowie `TOGGLE` für einen Taster, der den aktuellen Zustand nicht kennt. Alles Unbekannte bedeutet aus. In diesen Zustand sollte ein Netzspannungsschalter bei Fehlern wechseln.

Ein Payload für `led/<name>/set` ist eine Zahl von 0 bis 100 (ein openHAB-Dimmerprozentsatz) oder `ON` und `OFF` für die beiden Enden. Wenn sich zwei Lesarten widersprechen könnten, gilt die Zahl: `1` bedeutet ein Prozent und nicht „ein“.

Nichts wird gespeichert. Nach einem Neustart sind die Ausgänge ausgeschaltet. Die beibehaltenen `set`-Nachrichten des Brokers stellen sie eine Sekunde nach der Verbindung wieder her. Die Statustopics werden bei jeder Änderung und erneut nach jeder Verbindung veröffentlicht.

Um ein Relais oder eine LED über openHAB zu verwenden, binden Sie es als MQTT-Thing-Kanal ein:

```
Type switch : hallLight "Hall light" [ stateTopic="oheztouch/oheztouch-new/relay/1",
                                       commandTopic="oheztouch/oheztouch-new/relay/1/set" ]
Type dimmer : moodRed    "Mood red"   [ stateTopic="oheztouch/oheztouch-new/led/red",
                                        commandTopic="oheztouch/oheztouch-new/led/red/set" ]
```

Fügen Sie das Element zur Sitemap hinzu, und das Bedienfeld zeichnet dafür wie für jedes andere einen Schalter. Deshalb gibt es kein integriertes Widget für das lokale Relais.

Der Simulator hat standardmäßig weder Relais noch LEDs. `OHEZ_OUTPUTS=1` stattet ihn mit drei Relais und einem dreikanaligen Stimmungslicht aus, die nur als Protokollzeilen existieren. Siehe [Simulator](simulator_de.md#umgebungsvariablen).
