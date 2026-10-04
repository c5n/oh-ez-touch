# Systemarchitektur

OhEzTouch ist eine ESP32-Firmware, die mit ESP-IDF erstellt wird. Derselbe Quellcode erstellt außerdem einen Desktop-Simulator für Linux. Dieses Dokument gibt einen Überblick über die Schichten, die Hauptaufgaben und die Leistungsgrenzen.

## Schichten

```
+---------------------------------------------------+
| UI (LVGL)          pages, item screens, settings  |
+---------------------------------------------------+
| Application        openHAB client, MQTT, BLE,     |
|                    sensors, web interface         |
+---------------------------------------------------+
| Control            dimming policy, beeper queue   |
+---------------------------------------------------+
| Port layer         one implementation per target  |
| (main/port/)       esp32 | linux                  |
+---------------------------------------------------+
| Hardware           display, touch, backlight,     |
|                    beeper, relays, LEDs, radios   |
+---------------------------------------------------+
```

Alles oberhalb von `main/port/` wird von den Targets gemeinsam genutzt. Die Port-Schicht umfasst alles, was sich zwischen einem Panel und einem Desktop unterscheidet. Wenn eine Änderung außerhalb dieses Verzeichnisses ein `#if` für das Target benötigt, gehört sie wahrscheinlich stattdessen in einen neuen Port.

## Quellcode-Aufbau

```
main/                 main.cpp -- the entry point
main/config/          the settings, and the one field table that both the
                      panel's settings screen and the web form walk; the
                      PINs' arithmetic and their store
main/ui/              the LVGL user interface: the openHAB page, the settings
                      screen, the styles, themes and motion
main/ui/frames/       one per theme family: the chrome it draws around the
                      tiles, and where it lets them sit
main/ui/items/        one per openHAB item type: the screen its tile opens
main/openhab/         the openHAB client: the task every request waits on,
                      the sitemap model and parser it feeds, and the caches of
                      which servers are on the network and which sitemaps they
                      offer
main/mqtt/            the MQTT client: what the panel tells a broker, and the
                      one way the broker can talk back
main/ble/             the BLE beacon scanner: the advertisement parsers, and
                      the table of what is in range
main/peripherals/     the sensors: the BME280, and the timer that decides when
                      the next reading is taken
main/web/             the web interface: one renderer, one transport per target
main/net/             WLAN credentials, the radio state machine, and the one
                      mDNS question this firmware asks
main/control/         policy on top of the port layer: when to dim, and the
                      queue that plays a chime
main/sim/             the simulator's offline fixtures
main/testif/          the simulator's control interface: the UDP command
                      socket, the synthetic pointer, and the screen dumps
main/port/            the platform boundary: one implementation directory per
                      target
components/           LVGL and ArduinoJson as submodules, lodepng and TJpgDec
                      vendored
test/host/            the unit tests, as an IDF project of their own
```

## Die Einstellungstabelle

Jede Einstellung wird genau einmal beschrieben: in `main/config/config_fields.cpp`. Das Webformular, der Einstellungsbildschirm des Panels, der MQTT-Client und die Konfigurationsdatei durchlaufen alle diese Tabelle. Jede Zeile enthält den Namen, die Beschriftung, den Typ, den Standardwert, den Bereich oder die Optionen, den Speicherort in `config.json` und eine Kennzeichnung für „nur beim Start lesen“. Deshalb können die drei Oberflächen nicht auseinanderlaufen.

## PINs

Die beiden PINs ([Konfiguration](configuration_de.md#pins)) bestehen aus drei Schichten, die so aufgeteilt sind, dass der Teil, den ein Test erreichen kann, der Teil mit der Arithmetik ist.

- `main/config/pin_code.c` legt fest, was eine PIN ist (4 bis 8 Ziffern), hasht sie mit einem Salt und zählt falsche Versuche bis zu einer Sperre anhand einer Uhr, die der Aufrufer übergibt. Die Datei bindet nichts außer libc ein, und `test_pin_code.cpp` deckt sie ab. Die SHA-256-Implementierung ist eine eigene: IDF 6.1 liefert mbedtls 4 aus, das `mbedtls_sha256()` nicht mehr exportiert, und die PSA-API, die es ersetzt hat, ist im Host-Build nicht enthalten.
- `main/config/pin_store.c` speichert über `port_kv` einen gesalzenen Hash pro Geltungsbereich in NVS. Das hat denselben Grund wie bei den WLAN-Zugangsdaten: Die Werte überstehen `idf.py flash` und OTA. Außerdem hält es die PINs aus allem heraus, was die Einstellungstabelle versorgt. Eine PIN, die mit der des anderen Geltungsbereichs übereinstimmt, wird abgelehnt.
- `main/ui/ui_pin.cpp` ist das Tastenfeld und die Zugangssperre. `ui_pin_guard(scope, then, arg)` führt `then` sofort aus, wenn für den Geltungsbereich keine PIN gesetzt oder er entsperrt ist; andernfalls legt es das Tastenfeld auf `lv_layer_top()` und führt `then` nach einer korrekten Eingabe aus. Aufrufer fragen nie selbst ab, ob eine PIN erforderlich ist. Für einen neuen geschützten Bereich genügt ein Aufruf.

Zwei Aufrufer verwenden die Zugangssperre. `index_event()` des Einstellungsbildschirms leitet den Eintrag **System** über `PIN_SCOPE_SYSTEM`; jeder Systembereich ist nur über dieses Menü erreichbar, sodass diese eine Prüfung alle abdeckt. `event_handler()` der Kacheln leitet jede Kachel, deren Element das Tag `ohez-pin` trägt, über `PIN_SCOPE_ITEM`, und der JSON-Filter des Parsers lässt dafür `item.tags` durch. Ein Tipp, der während der PIN-Eingabe am Tastenfeld wartet, wird sowohl durch die zwei Verknüpfungen des Elements als auch durch dessen Slot nachverfolgt. Hat sich die Seite während der PIN-Eingabe darunter geändert, wird der Tipp verworfen, statt auf das Element zu wirken, das jetzt den Slot belegt.

Programmatische Einstiege verwenden die Zugangssperre nicht. Dazu gehören das automatische Öffnen des WLAN-Portals, `OHEZ_SETTINGS`, `OHEZ_ITEM` sowie `settings` und `nav` der Steuerschnittstelle.

`ui_pin_loop()` verwirft jede Entsperrung, wenn die Hintergrundbeleuchtung gedimmt wird oder eine Minute lang keine Berührung erfolgt. War der Systembereich entsperrt, ruft es `ui_settings_leave_protected()` auf. Diese Funktion speichert die geöffnete Systemseite und kehrt zum Einstellungsmenü zurück. Die Weboberfläche entfernt eine PIN über `ui_pin_request_clear()`: ein atomares Bit, das die nächste Loop-Iteration verarbeitet, weil ein Web-Handler LVGL nicht berühren und nicht mit den Lesezugriffen des LVGL-Tasks auf den Speicher konkurrieren darf.

## Der openHAB-Clienttask

Zum Erzeugen der UI muss openHAB abgefragt werden: eine Sitemap-Seite pro Navigationsebene, ein Icon pro Kachel und ein Elementstatus pro Kachel, sobald er sich ändert – siehe „Der Ereignisstream“ weiter unten.

Alle Anfragen warten auf einen eigenen Task (`main/openhab/openhab_client.cpp`). Die UI übermittelt eine URL und zeichnet weiter. Die Antwort kommt über eine Queue zurück. `openhab_ui_loop()` nimmt pro Iteration ein Ergebnis aus der Queue. Eine Seite erscheint, sobald ihr JSON geparst ist. Jede Kachel zeigt zunächst ihre Beschriftung, ihren Status und ein Platzhalter-Wasserzeichen. Die Icons werden in den folgenden Frames ergänzt. Die Uhr läuft weiter und die Touchbedienung reagiert, auch wenn openHAB nicht erreichbar ist.

Der Worker ruft keine `lv_*`-Funktion auf. Er liest weder `Item`, `Sitemap` noch `Config`. URLs gehen hinein und Bytes kommen zurück. Jede Entscheidung darüber, was ein Body bedeutet, und jeder LVGL-Aufruf bleiben auf dem Task, dem der Bildschirm gehört. `main/openhab/openhab_connector.cpp` ist ein Modell und ein Parser ohne Wissen um ein Netzwerk. Dadurch können die Host-Tests den Parser abdecken.

Der Parser benennt die Schlüssel, die er benötigt; alles andere wird bereits beim Parsen verworfen, statt gespeichert und später übersprungen zu werden. Eine Seite enthält einiges, wofür das Panel keine Verwendung hat – Quellen von Beschriftungen, Einheiten, Elementkategorien, Gruppenmitglieder, Zeitstempel. Das Speichern kostete mehr als das Speichern der tatsächlich gelesenen Daten: Auf der Demo-Seite mit sechs Widgets waren es 5892 Byte Dokument gegenüber 3136 mit dem Filter und 188 Allokationen gegenüber 103. Element-Tags standen auf dieser Liste, bis die Element-PIN hinzukam; sie werden jetzt gelesen und sind bei den meisten Elementen ein leeres Array. Das bedeutet auch, dass zukünftige Felder, die openHAB hinzufügt, das Panel nichts kosten. Der Preis dafür: Ein vom Parser gelesenes Feld muss auch im Filter benannt sein; andernfalls wird es als null gelesen.

### Der Ereignisstream

Elementstatuswerte werden übertragen, nicht abgefragt. `main/openhab/openhab_events.cpp` hält einen Server-Sent-Event-Stream in einem Task und mit einem eigenen `esp_http_client`-Handle offen: Ein Stream endet nie, weshalb er auf dem Worker jede Seite, jedes Icon und jeden Befehl dahinter aufhalten würde. Alle halbe Sekunde bietet die UI den sichtbaren Inhalt an: die Seite und ihre Elemente. Ein unverändertes Angebot kostet einen Vergleich.

**Zuerst kommt der Sitemap-Stream.** Bei jeder Verbindung wird `POST /rest/sitemaps/events/subscribe` für eine neue Abonnement-ID ausgeführt. Eine gespeicherte ID taugt nicht: Sobald openHAB sie freigegeben hat, wird eine GET-Anfrage dafür nie beantwortet. Anschließend öffnet der Task `GET /rest/sitemaps/events/<id>?sitemap=<sm>&pageid=<page>`. Jedes Ereignis entspricht einem Widget dieser Seite, so wie openHAB es gerade zeichnen würde:
- der rohe Status des Elements
- die Beschriftung mit dem formatierten Wert in `[...]`
- Beschriftungs-, Wert- und Iconfarbe, die von den Regeln der Sitemap ausgewählt wurden
- die Sichtbarkeit des Widgets

Ereignisse werden anhand eines Hashs der `widgetId` den Kacheln und Uhrzeilen zugeordnet. Das Seiten-JSON enthält dieselben IDs. Wenn keine Regel zutrifft, fehlt die Farbe und ist nicht leer; fehlt sie, wird also die Farbe des Themes verwendet. Eine Änderung, die das Layout beeinflusst, lädt die Seite neu:
- ein Widget einer Kachel wird ausgeblendet
- ein Widget, das die Seite nicht enthielt, meldet sich als sichtbar
- `SITEMAP_CHANGED`
- eine geänderte Statusbeschreibung
- der Stream kommt nach einer vorherigen Verbindung wieder zurück, damit eine verpasste Änderung nicht bestehen bleibt
- ein verworfenes Ereignis

Ein Widget, für das nach einem solchen Neuladen weiterhin kein Platz vorhanden ist – etwa ein Element eines ausgeblendeten Frames, das sich selbst als sichtbar meldet –, wird bis zur nächsten Seite gespeichert, sodass es nur einmal neu lädt. Während eine Elementsteuerung geöffnet ist, wartet das Neuladen. Bei einem Seitenwechsel wird ein neuer Stream geöffnet. Ändern sich nur die Elemente, etwa wenn der Uhrbildschirm erscheint, bleibt der Stream bestehen.

openHAB sendet auf diesem Stream zunächst nichts, nicht einmal die Statuszeile, bis das erste Ereignis eintrifft. Auf einer ruhigen Seite ist das der Keepalive, ungefähr einmal pro Minute. Daher wartet der Task mit `select()` in Viertelsekundenschritten auf dem Socket, damit ein Seitenwechsel weiterhin erkannt wird. Der Watchdog steht auf 130 s.

**Der Elementstream** ist der Fallback für einen Server, der das Abonnement ablehnt (404, 401). Er wird einmal pro Server angefragt. Der Task hält `GET /rest/events?topics=*/items/<name>/statechanged,...` offen; die sichtbaren Elemente – Kacheln und Uhrelemente – bilden die Themenliste. Eine geänderte Liste erfordert eine neue Verbindung. Das `value` jedes Ereignisses ist der rohe Status, den `/rest/items/<name>/state` liefert. Ein übertragener Status durchläuft daher denselben Weg wie ein abgefragter: `Item::applyState()`, die Kachel, die geöffnete Elementsteuerung, das Icon. Farben und Sichtbarkeit bleiben so, wie sie beim Laden der Seite waren.

Das Parsen beider Streams erfolgt in `main/openhab/openhab_event_parse.cpp`, das keinen Socket enthält und von den Host-Tests abgedeckt wird. Ein Sitemap-Ereignis enthält das vollständige Element und ist 900 Byte lang, deshalb ist der Zeilenpuffer 2 KB groß. Eine längere Zeile wird verworfen und gemeldet. Das eine Element, dessen Ereignisse immer länger sind, ist ein Image-Element, dessen Status das Bild ist: Jeder JSON-String mit mehr als 512 Byte wird vor dem Aufteilen der Zeile gekürzt (`main/openhab/json_squeeze.c`), sodass ein neuer Snapshot ein gewöhnliches Ereignis ist.

**Die Türklingel** hat einen eigenen Stream (`main/openhab/openhab_bell.cpp`), weil die Klingel auf keiner Seite liegt: `GET /rest/events?topics=*/items/<ring>/statechanged`, der nur geöffnet wird, wenn ein Klingelelement konfiguriert ist.

Die Abfrage bleibt als Fallback bestehen. Während der Stream nicht verfügbar ist – openHAB startet neu, die Verbindung kommt zurück, ein Server ohne diesen Endpunkt –, wird jede Kachel wie bisher alle fünf Sekunden abgefragt. Während der Stream verfügbar ist, geschieht das einmal pro Minute und fünf Sekunden nach einem Tipp, der den Status geändert hat: Ein verworfener oder abgelehnter Befehl ändert nichts auf dem Server, daher würde kein Ereignis die Kachel zurücksetzen. Nach einer erneuten Verbindung werden Ereignisse nicht wiedergegeben; deshalb werden bei jeder Verbindung und bei jedem wegen fehlender Queue-Kapazität verworfenen Ereignis alle Kacheln sofort einmal abgefragt. Beim Elementstream senden openHAB 3.4 und neuere Versionen alle zehn Sekunden einen Keepalive; dreißig Sekunden ohne Byte gelten als ausgefallener Stream.

Zwei weitere Anfragen nutzen den Worker gemeinsam:

- **Die Sitemap-Liste.** `main/openhab/openhab_sitemaps.cpp` ruft die Liste der von einem Server angebotenen Sitemaps ab. Beide Einstellungsoberflächen zeigen sie neben dem Feld „Sitemap“ an. Ein Cache bedient beide.
- **Bilder.** Das Bild eines Image-Elements für den Bildschirmausschnitt (`main/ui/items/item_image.cpp`). Es verwendet nicht den 12-KB-Puffer und wird nie vollständig zwischengespeichert: `GET <item>/state` mit `Accept: image/*` liefert direkt das JPEG. TJpgDec (`main/openhab/image_decode.c`) liest es über einen 512-Byte-Puffer und einen 3,6-KB-Arbeitsbereich aus dem Socket, verkleinert es beim Dekodieren um den Faktor 1/2 bis 1/8 und überspringt die übrigen Pixel. Die einzige wachsende Allokation ist das Ergebnis – höchstens halb so breit und hoch wie der Bildschirm, 29 KB im Querformat. Wenn der größte freie Speicherblock kleiner ist, fordert der Bildschirm weniger an.
- **Servererkennung.** `main/openhab/openhab_discover.cpp` sendet eine einzelne mDNS-Abfrage (`main/net/mdns_query.c`) über einen eigenen UDP-Socket und liest die Antworten, ohne zu blockieren. Ein 44-Byte-Datagramm wird zweimal gesendet; das Zeitfenster beträgt zweieinhalb Sekunden. Dahinter steckt keine `espressif/mdns`-Komponente. Das gesamte Feature benötigt etwa 4 KB Flash und läuft für die Tests auch auf dem Host.

Anfragen werden mit einer Generation versehen. Die Generation wird bei jedem Abruf einer Seite erhöht. Icons und Statuswerte für eine Seite, von der der Benutzer weg navigiert ist, werden verworfen. Befehle sind davon ausgenommen: Ein vom Benutzer angeforderter Befehl wird auch nach dem Navigieren noch zugestellt.

Es gibt einen Worker und eine Queue, keinen Pool. Ein Pool könnte zwei Tipps auf dasselbe Element in falscher Reihenfolge zustellen. Die Verbindung zu openHAB bleibt zwischen Anfragen offen. Die sechs Icons einer Seite teilen sich einen TCP-Handshake.

Eine Seitenanfrage wird an den Anfang der Queue gestellt. Der Worker ist ein einzelner Task und eine laufende Anfrage kann nicht abgebrochen werden; lag eine Seite hinter einer Queue von Statusabfragen, musste sie auf alle warten. Die überholten Abfragen gehören zur gerade verlassenen Seite und wären ohnehin verworfen worden, ohne ausgeführt zu werden.

Bei einer Änderung der Verbindung wird die Verbindung getrennt. Eine TCP-Verbindung, die eine erneute Assoziierung überstanden hat, ist tot. Da nur die WLAN-Zustandsmaschine auf dem Panel weiß, dass die Verbindung unterbrochen war, meldet sie dies (`openhab_http_reset()`). Ohne das würde die erste Anfrage jedes solchen Zeitraums in den toten Socket schreiben – was gelingt – und dann die gesamte Zeitüberschreitung auf eine Antwort warten, die nicht kommen kann.

Antwort-Bodies werden in einen einzelnen Puffer gelesen, der einmalig alloziert und auf die größte Klasse bemessen wird: eine 12-KB-Sitemap-Seite. Eine Seite wird beim Einlesen gefiltert, damit der Status eines Image-Elements – das Bild als Base64 in `state` und noch einmal in `lastState` – die Seite nicht auf 100 KB anwachsen lässt; siehe `json_squeeze.h`. Ein pro Seitenaufruf alloziierter Puffer benötigte 12 KB zusammenhängenden Heap. Bei einer schwachen Verbindung hält der WiFi-Treiber seine Sendepuffer jedoch so lange, wie ein Frame erneut übertragen wird. So forderte ein Seitenabruf den größten verfügbaren Block genau dann an, wenn der Heap am wenigsten hergab. Das Symptom war ein Panel, dessen Kacheln sich weiter aktualisierten, während jede aufgerufene Unterseite fehlschlug. Ein Elementstatus braucht das nicht einmal: Er wird im Ergebnis mittransportiert, daher verursacht eine Abfrage überhaupt keine Allokation.

### Speicherbelegung beim Laden einer Seite

Nichts – darauf zielt das Design ab, und auf dem Panel ist das auch die Antwort. Der oben beschriebene Puffer ist alles; er wird doppelt genutzt: Der Body der Seite liegt am Anfang, und der Bereich dahinter dient als Pool, aus dem das JSON-Dokument seine Allokationen bezieht (`JsonArenaAlloc` in `main/openhab/openhab_connector.cpp`, als Scratch-Speicher an `Sitemap::parse()` übergeben). Keine Kopie des Seiten-Bodys, kein mehrere Kilobyte großer Dokumentpool auf dem Heap – die beiden Allokationen, deren Bereitstellung ein fragmentierter Heap als Erstes verweigerte; daraus entstand das Problem „Die Zurück-Taste kann die Hauptseite nicht laden“. Der Handshake besteht aus einem Flag: Ein Ergebnis, dessen Payload der Puffer selbst ist (`payload_static`), hält den Worker aus dem Puffer heraus, bis die UI ihn freigibt. Die UI gibt jedes Ergebnis frei, das sie entgegennimmt. Eine Seite, die den hinteren Bereich überschreitet, wechselt zu einem heapgestützten Dokument und schreibt eine Logzeile – die Arena ist der schnelle Pfad, keine zweite Möglichkeit, eine Seite zu verlieren. Der Spitzenbedarf für Flottenseiten nach dem Filtern liegt gemessen bei 4,9 bis 5,8 KB Pool. Ein 12-KB-Body-Puffer kann also bei Seiten mit bis zu etwa sechs Widgets Body und Dokument aufnehmen.

Die Icons einer Seite sind die andere Hälfte derselben Betrachtung. Der eingebaute Satz besteht aus indizierten Datensätzen, die LVGL ohne Dekodierung liest (siehe [Icon-Satz](icon-set_de.md)); ein abgerufenes PNG wird auf dem UI-Task dekodiert und – falls der Icon-Satz des Servers größer als die 32 px des Panels ist – vor dem Speichern in der Kachel darauf verkleinert. Der Decoder ist eine angepasste lodepng-Version: In-place-Unfilterung, keine Konkatenationskopie für eine Datei mit einzelner IDAT und eine Reservierung, die schnell fehlschlägt, statt in eine Reallocationsspirale zu geraten. Dadurch sinkt der Spitzenbedarf einer 64x64-RGBA-Icon-Dekodierung von etwa 37 KB gleichzeitig benötigtem Heap auf etwa 31 KB. Jede Anpassung ist im Quellcode mit `OH-EZ-TOUCH` markiert, damit sie beim Aktualisieren von lodepng wiedergefunden wird.

Zwei benachbarte Komponenten kommen hinzu, weil sie beim Laden einer Seite um denselben Heap konkurrieren. Ein BLE-Scanfenster wird verschoben, solange das Panel berührt wird, und frühzeitig beendet, wenn es geöffnet ist ([ble_de.md](ble_de.md) beschreibt den Arbeitszyklus). Und eine Überblendung, die LVGL um einen Ebenenpuffer bittet, den der Heap nicht mehr bereitstellen kann, wird einfach übersprungen (`UI_FADE_MIN_LARGEST_BLOCK` in `main/ui/ui_motion.cpp`). Stattdessen wird das Objekt direkt angezeigt. Das ist in diesem Moment am ehesten verzichtbar und deutlich besser als die Alternative: `LV_USE_ASSERT_MALLOC` ist jetzt 0, weil der Handler mit `while(1)` endlos wartet und eine fehlgeschlagene Allokation das Panel eingefroren hat.

Ein vom Netzwerk abgeschnittener Body ist ein Fehler, keine kurze Antwort. Diese Unterscheidung wird in `main/openhab/openhab_http.cpp` vorgenommen und muss manuell erfolgen: `esp_http_client_read_response()` meldet einen Lese-Timeout als nichtnegative Byteanzahl – genau wie einen vollständigen Body. Eine Statusabfrage mit Timeout kam daher früher als *erfolgreiche* Abfrage mit leerem Body an und wurde vom Element gespeichert: Ein Schalter zeigte nichts an und eine Temperatur wurde als null angezeigt, obwohl der Server des Panels einwandfrei funktionierte und lediglich das Signal schwach war.

## Was als Berührung gilt

Ein Druck muss zweimal gelesen werden, bevor er als solcher zählt. `read_cb()` in `main/port/esp32/port_indev.c` hält den ersten druckähnlichen Messwert als Kandidaten zurück und meldet noch nichts. Der Druck beginnt erst, wenn der nächste Poll – einen LVGL-Frame später – innerhalb eines Zwanzigstels des Bildschirms davon landet.

Der Grund ist, dass der Treiber einen Finger nicht von einer Störung unterscheiden kann. Eine XPT2046-Messung ist eine Druckprobe über einem Schwellenwert, gefolgt von fünf direkt hintereinander und innerhalb derselben SPI-Transaktion aufgenommenen Koordinatenproben – insgesamt etwa zweihundert Mikrosekunden. Die Mittelwertbildung sieht wie eine Filterung aus, ist aber keine: Alle fünf Proben stammen vom selben Zeitpunkt. Alles, was länger als ein Fünftel einer Millisekunde anhält, kommt daher als fünf übereinstimmende Proben an. Ein resistives Panel neben einer schaltenden Hintergrundbeleuchtung und einem Funkmodul erzeugt solche Messwerte; der Leser wird so lange abgefragt, wie das Panel eingeschaltet ist, und zwar zweiundsechzigmal pro Sekunde.

TFT_eSPI erledigte diese Bestätigung mit `getTouch()` für die Arduino-Firmware; beim Port zu `esp_lcd_touch` ging sie verloren. Die Folge war normalerweise kein verirrter Tipp: Das Panel wird nach einer Minute gedimmt, und ein im gedimmten Zustand eintreffender Druck wird von `ohez_touch_wake()` abgefangen, das den Weckton abspielt und den Druck verschluckt – ein kurzer Piepton und sonst nichts. Ein Phantomtipp auf ein Widget war seltener, weil das Panel die meiste Zeit gedimmt ist.

Die Regel gilt auch für das kapazitive Board, da es für beide Controller nur einen Leser gibt. Beim Lanbon ist sie nicht erforderlich – ein FT6336 meldet nur eine Berührung, die er bereits selbst erkannt hat –, und die Latenz von einem Frame ist es nicht wert, dafür einen zweiten Codepfad einzuführen.

Nur der Beginn eines Drucks wird bestätigt. Sobald er erkannt ist, wird jeder Messwert geglaubt, weil sich ein ziehender Finger tatsächlich weiter als die Toleranz bewegt und das Loslassen weiterhin beim ersten leeren Poll gemeldet wird. Der Preis sind eine Frame-Latenz beim Drücken und die Tatsache, dass ein kürzer als zwei Polls dauernder Tipp nicht als Tipp gilt – mit einem Finger ist das ohnehin nicht erreichbar.

Was ein bestätigter Druck bedeuten kann, steht in `main/ui/ui_input.c`. Es gibt nur die Tippgeste: Der Klick wird beim Drücken ausgelöst, nichts scrollt, und die Scrollgrenze sowie die Gestenschwellen des Eingabegeräts liegen bei 255 px, sodass ein bewegter Finger nicht zu einer Wischbewegung wird. Eine Ausnahme ist ein Schieberegler. LVGL beginnt erst dann mit dem Ziehen eines Schiebereglers, wenn der Finger die Scrollgrenze zurückgelegt hat. Bei einem Druck auf einen Schieberegler wird die Grenze daher auf 4 px gesenkt, bis der Finger angehoben wird.

## Bildschirme, Frames und Bewegung

Die UI basiert auf drei Konzepten.

**Ein aufgesetzter Bildschirm.** `ui_screen` verwaltet eine Wurzel und höchstens ein darüberliegendes Element: eine Elementsteuerung, den Einstellungsbildschirm oder den Uhrbildschirm. Es handelt sich nicht um einen allgemeinen Stack. Auf dem Panel gibt es genau diese drei Fälle.

**Das Dimmen ist eine Zustandsmaschine.** `BacklightControl` (`main/control/`) verwaltet das Aktivitäts-Timeout und führt die Hintergrundbeleuchtung anhand von Deadlines durch die Zustände, weil ein LEDC-Fade asynchron läuft. Ist der Uhrbildschirm ausgeschaltet, besteht das Dimmen wie bisher aus einem Fade-out und das Aufwecken aus einem Fade-in. Ist er eingeschaltet, führen beide Vorgänge über Schwarz, sodass der Bildschirmwechsel unbemerkt ablaufen kann:

```
awake -> sleep_out (1.5 s to 0) -> sleep_dark -> sleep_in (0.8 s to dim) -> asleep
asleep -> wake_out (0.1 s to 0) -> wake_dark -> wake_in (0.18 s to normal) -> awake
```

Daraus ergeben sich zwei Flags. `isInactive()` wird beim Auslösen des Timeouts wahr. `main.cpp` reagiert auf diese Flanke mit `openhab_ui_request_home()`, sodass die Hauptseite geladen wird, während das Licht ausblendet. `isDimmed()` ändert sich nur in den beiden dunklen Phasen. `ui_clock_loop()` legt dann seinen Bildschirm auf oder nimmt ihn herunter und zeichnet den neuen Bildschirm sofort mit `lv_refr_now()`, damit beim Aufleuchten der richtige Bildschirm zu sehen ist. Eine Berührung während `sleep_out` kehrt direkt um, ohne den Bildschirm zu wechseln. Die Zeigerunterdrückung nach einem Wecktipp (350 ms, `port_indev.c`) dauert länger als der Weckvorgang, sodass der Finger die darunterliegende Seite nie erreicht.

Die drei Elemente des Uhrbildschirms sind der Uhren-Frame der Hauptseite (siehe [Sitemaps](sitemap_de.md#der-uhren-frame)). Sie liegen in den Slots 6 bis 8 von `widget_context[]`, hinter den Kacheln. Dadurch werden sie wie die Kacheln abonniert, abgefragt, beantwortet und mit Icons versehen, anhand desselben Slots und derselben Seitengeneration. Sie haben keine Kachel. Solange der Uhrbildschirm aktiv ist, stellt er seine Beschriftungen und sein Bild mit `openhab_ui_clock_attach()` bereit. Ohne eine solche Zuordnung werden sie weder abgefragt noch in die Themenliste des Ereignisstreams aufgenommen.

**Ein Frame pro Theme-Familie.** In `main/ui/frames/` wird aus einem Theme mehr als nur eine Farbpalette. Jede Familie erstellt ihren eigenen Rahmen und gibt mit `content_area()` das Rechteck zurück, das das Kachelraster verwenden darf. So kann LCARS links eine Leiste platzieren, Material ganz ohne Rahmen auskommen und Reticle zwei feine Linien zeichnen. Die Seite weiß nichts davon. Eine Familie kann außerdem jede Kachel über `decorate_tile()` gestalten. LCARS füllt Kacheln abhängig von ihrem Typ. Reticle ersetzt den Kachelrand durch Eckklammern: Bei einer Kachel, die zu einer Seite führt, wachsen und schrumpfen deren Arme; eine bedienbare Kachel hat statische Klammern, und eine einfache Anzeige kurze, blasse Klammern. Das Atmen zeichnet nur die vier Eckquadrate neu und nur, wenn sich ein Arm um einen ganzen Pixel ändert. Classic verwendet den gemeinsamen Frame in `ui_frame_common.cpp`. Das Raster wird von `ui_geometry.hpp` berechnet, das frei von `<lvgl.h>` ist, damit die Host-Tests es prüfen können.

**Zwei Ausrichtungen.** Bei einem hochkant montierten Panel wird `Orientation` auf `portrait` gesetzt und das Display mit 240x320 statt 320x240 erstellt – eine Hardwareausrichtung, keine Softwaredrehung: MADCTL des Panels behält seine eigenen Achsen bei, sodass ein Frame genau so viel kostet wie im Querformat. Die Einstellung gilt nur beim Start, da MADCTL während der Initialisierung festgelegt wird. Die Touchkalibrierung galt ebenfalls nur beim Start, bis daraus vier Einstellungen und ein Verfahren wurden. Jetzt ist sie eine Berechnung beim Einlesen des Zeigers (`main/port/touch_cal.c`), und eine Änderung wird beim nächsten Druck wirksam. Jeder Eintrag der Theme-Tabelle enthält dafür ein zweites Raster (zwei Spalten mit je drei Kacheln), jeder Frame misst seinen Rahmen anhand der von LVGL gemeldeten Auflösung, und die Seite fragt `ui_style_grid()` ab, welches der beiden Raster verwendet werden soll. Die Geometrie-Host-Tests prüfen beide Ausrichtungen anhand derselben Mindestfläche für einen Finger.

Ein Frame besteht nicht aus Style-Eigenschaften, daher kann `lv_obj_report_style_change()` ihn nicht erreichen. Bei einem Wechsel des Themes zur Laufzeit wird der Rahmen der alten Familie *vor* `ui_style_select()` entfernt und der neue danach aufgebaut.

**Ein Bildschirm pro Elementtyp.** `main/ui/items/` enthält eine Datei pro openHAB-Elementtyp, die über eine Registry gefunden wird. Jede enthält einen `refresh`-Hook, damit eine geöffnete Steuerung den Serveränderungen folgt. Der Bildschirm des Players zeigt außerdem andere Kacheln seiner Seite: Titel, Interpret und Lautstärke in seinem Frame. `Item::getFrame()` merkt sie sich, nachdem beim Parsen die Frames abgeflacht wurden. Sein Deskriptor setzt `follows_page`, sodass eine Änderung irgendeiner Kachel den Bildschirm aktualisiert; die Kacheln liest er über `item_screen_page_item()`. Er braucht keinen eigenen Abruf und kein zweites Abonnement: Die Kacheln sind auf der Seite, also verfolgt sie der Ereignisstream bereits. Die H-, S- und V-Felder des Farbbildschirms zeigen den Bereich, den sie durchlaufen, als horizontalen Farbverlauf; beim Verschieben eines Feldes werden die anderen beiden neu eingefärbt. Dafür sind zwei Einstellungen in `lv_conf.h` erforderlich: `LV_GRADIENT_MAX_STOPS` auf 7 für den Farbtonkreis und `LV_DRAW_SW_SUPPORT_RGB888` eingeschaltet. LVGL mischt einen horizontalen Farbverlauf aus einer RGB888-Farblinie. Ist dieses Format ausgeschaltet, zeichnet es nichts.

### Welche Animationen das Panel verkraftet

Der begrenzende Faktor ist nicht die CPU. Ein RGB565-Panel mit 320x240 Pixeln bewegt über einen 40-MHz-SPI-Bus 5 MB/s. Ein vollständiges Neuzeichnen des Bildschirms erfordert **30,7 ms reine Übertragungszeit**. Das Rendern kostet etwa ein Viertel davon. Die Framezeit wird durch die SPI-Zeit bestimmt. Das Budget ist ein *Pixelbudget*: ungefähr 24.000 px pro Frame für dauerhafte Animationen, 40.000 für eine einmalige Animation.

Deshalb gibt es auf häufig genutzten Bedienpfaden keine Bildschirmübergänge. `lv_screen_load_anim()` bewegt das Bildschirmobjekt. Jeder Frame invalidiert 320x240 zweimal. Eine 200-ms-Wischbewegung verursacht sechs Frames mit vollständigem Neuzeichnen. Der Einstellungsbildschirm ist die einzige Ausnahme und wird selten aufgerufen. Alles andere lässt den Bildschirm stehen und staffelt stattdessen den Inhalt.

`ui_motion.hpp` dokumentiert die beiden Konstrukte, die kostenlos aussehen, es aber nicht sind: `opa_layered` und `transform_scale`. Ein einfaches `lv_obj_set_style_opa()` blendet einen gesamten Unterbaum ohne Layer ein oder aus. `transform_width` und `transform_height` verformen eine Fläche auf dieselbe Weise.

Eine Regel verhindert hier die meisten Abstürze: **`var` jeder Animation muss das `lv_obj_t` sein, das animiert wird**, niemals eine Kontextstruktur. `lv_obj_delete()` bricht Animationen ab, deren Schlüssel das gelöschte Objekt ist.

## Wofür die Framezeit verwendet wird

Ein ESP32 besitzt keinen 2D-Beschleuniger. Jedes Pixel wird von einer der Softwareschleifen in `src/draw/sw/` von LVGL geschrieben. Anschließend wird der fertige Streifen über SPI zum Panel übertragen. Diese beiden Kosten begrenzen alles, was die UI tun kann.

**Die SPI-Seite lässt sich berechnen.** Der Bus läuft mit 40 MHz. Beim Lanbon ist das die Obergrenze des ESP32 selbst, da dessen SPI-Pins über die GPIO-Matrix geroutet werden. Bei den ArduiTouch-Boards ist es die höchste sinnvolle Geschwindigkeit für einen ILI9341. Ein vollständiges Neuzeichnen des Bildschirms umfasst 320 x 240 x 2 Byte bei 40 Mbit/s und dauert unabhängig von der CPU-Geschwindigkeit etwa 31 ms. Eine Teilaktualisierung kostet entsprechend ihrer Fläche. Deshalb besteht die UI aus Kacheln, die einzeln invalidiert werden.

**Auf der CPU-Seite greifen die Einstellungen.** `main/port/esp32/port_display.c` rendert in zwei DMA-fähige Puffer und wechselt zwischen ihnen. Das Rendern eines Streifens überlappt mit der Übertragung des vorherigen. Bleibt das Rendern unter den 3 ms, die die Übertragung benötigt, ist der SPI-Wert die tatsächliche Untergrenze. Dazu tragen fünf Dinge bei:

- **Core 1 statt Core 0.** `CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1` in `sdkconfig.defaults.esp32`. IDF startet den Haupttask auf Core 0, wo auch der WiFi-Task, der Bluetooth-Controller, der NimBLE-Host und der esp_timer-Task laufen. Der ESP32 hat pro Core einen 32-KB-Cache. Auf Core 0 verdrängte der Funkcode die Render-Schleifen und die Font-Glyphen.
- **240 MHz statt 160.** `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240`. Ein netzbetriebenes Wandpanel muss nicht an Strom sparen.
- **LVGL wird mit `-O2` kompiliert.** Der Rest des Projekts verwendet `-Os`. `components/lvgl/CMakeLists.txt` legt die Ausnahme fest. Sie kostet 19 KB einer 1920-KB-App-Partition.
- **`LV_USE_ASSERT_OBJ` ist auf dem Gerät ausgeschaltet** und im Simulator eingeschaltet. Die Prüfung durchläuft bei nahezu jedem öffentlichen LVGL-Aufruf alle Objekte aller Bildschirme.
- **Die Schleife wartet so lange, wie LVGL vorgibt**, und nicht pauschal 5 ms. `ohez_loop()` übernimmt den Wert von `lv_timer_handler()` und wartet entsprechend, begrenzt auf 10 ms, damit die anderen Schleifen weiterlaufen.

Zwei Dinge sehen wie Stellschrauben aus, sind es aber nicht. Der Byte-Tausch in `flush_cb()` macht etwa 4 % eines Flushs aus; würde stattdessen in `LV_COLOR_FORMAT_RGB565_SWAPPED` gerendert, läge die Mehrarbeit bei jedem gemischten Pixel. Auch `LV_DRAW_LAYER_SIMPLE_BUF_SIZE` ist hier eine wirkungslose Konfiguration: `ui_motion.hpp` schließt jede Eigenschaft aus, die ein Objekt zu einem Layer machen würde.

### Messung

Das Panel misst sich selbst. Derselbe Code läuft im Simulator.

`main/ui/ui_frame_probe.c` hängt sieben Event-Callbacks an das Display. LVGL markiert damit einen Frame: `REFR_START`/`REFR_READY` um die Aktualisierung, `RENDER_START`/`RENDER_READY` um das Zeichnen, `FLUSH_START` mit der Fläche des Streifens sowie `FLUSH_WAIT_START`/`FLUSH_WAIT_FINISH` um das Warten auf die vorherige Übertragung. `main/ui/ui_frame_stats.c` bildet den Mittelwert über ein Fenster von 64 Frames oder zehn Sekunden. Das Ergebnis wird an drei Stellen veröffentlicht: in den Zeilen **Frame** auf der Webstatusseite, über MQTT als `system/fps`, `system/render_us` und `system/wait_us` sowie im `frame`-Objekt von `status` der Testoberfläche.

**`render_us` im Vergleich zu `wait_us` bestimmt den nächsten Schritt.** Überwiegt die Wartezeit, ist das Panel durch die oben genannten 31 ms SPI-Übertragungszeit begrenzt. Nur der Bustakt hilft. Überwiegt die Renderzeit, lohnen sich die nachfolgenden Stellschrauben auf der CPU-Seite trotz ihrer Risiken.

### Was noch möglich ist und was es braucht

Es bleiben vier Stellschrauben. Jede erfordert ein Board statt einer bloßen Diskussion:

- **80 MHz SPI auf den ArduiTouch-Boards.** Die einzige Stellschraube, die direkt die 31 ms verändert. Die Display-Pins stimmen genau mit dem IOMUX überein. Ein ILI9341 liegt bei 40 MHz bereits oberhalb seiner Datenblatt-Schreibzykluszeit; das muss also gemessen werden. Ein Fehlschlag zeigt sich als sichtbare Beschädigung, nicht als unbrauchbares Gerät.
- **Flash im QIO-Modus.** Derzeit `CONFIG_ESPTOOLPY_FLASHMODE_DIO`. Der Zeichencode von LVGL und 1,3 MB Font-Glyphen werden direkt aus dem Flash gelesen. QIO halbiert ungefähr die Kosten eines Cache-Misses. Ein Fehlschlag zeigt sich darin, dass ein Modul nicht startet.
- **`LV_ATTRIBUTE_FAST_MEM` als `IRAM_ATTR`.** In `lv_conf.h` ist der Wert leer. Damit würden die häufig verwendeten Blend-Schleifen im IRAM liegen. Etwa 33 KB IRAM sind frei, aber dieser Speicher wurde für den Bluetooth-Controller freigegeben. Vor einer Nutzung muss die Map-Datei geprüft werden.
- **Die Streifenhöhe, `DRAW_BUFFER_LINES`.** Höhere Streifen durchlaufen den Objektbaum pro Frame seltener. Das hilft nur, wenn das Rendern den Wettlauf gegen die Übertragung verliert. Die Puffer werden aus dem Heap alloziert, bevor die Funkmodule starten.

Noch ein Punkt: LVGL wartet in einer aktiven Schleife auf den DMA (`while(disp->flushing);`). Die Schleife endet mit der Übertragung, sodass kein Frame verspätet ist. Die CPU sieht aber voll ausgelastet aus, obwohl sie nichts zu tun hat. Die Stellschrauben auf der CPU-Seite bringen daher weniger, als ihr Anteil an einem Frame vermuten lässt.

## Der Simulator als Architekturnachweis

Der Simulator ist kein reduzierter Build. Er verwendet dieselben Quelltexte, dasselbe LVGL, dieselben Fonts und Styles. Nur `main/port/` unterscheidet sich. Das ist eine bewusste Architektureigenschaft: Alles oberhalb der Port-Schicht wird täglich auf dem Host getestet und verwendet. Siehe [Simulator](simulator_de.md) und [Tests](testing_de.md).
