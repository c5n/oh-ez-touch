# Tests

## Host-Unit-Tests

Die Unit-Tests sind ein eigenes ESP-IDF-Projekt auf demselben `linux`-Target wie der Simulator:

```bash
cd test/host
idf.py --preview set-target linux
idf.py build && ./build/oh-ez-touch-host-test.elf
```

Die Binärdatei beendet sich mit der Anzahl der Fehler. Sie kann in einem Skript verwendet werden.

Jede aus `main/` in die Tests eingebundene Datei berührt weder LVGL noch das Netzwerk. Das ist beabsichtigt. Die Beacon-Parser erhalten rohe Ankündigungsbytes aus der Port-Schicht (`main/port/port_ble.h`), und der Multipart-Scanner ist ein Sink; deshalb können beide auf dem Host getestet werden.

### Testsuiten

- **test_item_setters**: die String-Setter von `Item`. Sie kopieren Beschriftungen, Statuswerte, Muster und Zuordnungen unbekannter Länge aus dem Sitemap-JSON. Die Kopien müssen sauber abgeschnitten werden. Ein Canary hinter dem Objekt erkennt Fälle, in denen das nicht geschieht.
- **test_ui_theme**: die Theme-Namensabfragen in `main/ui/ui_theme.hpp`. Bei einem Tippfehler, einer leeren Zeichenkette und NULL muss der Fallback greifen. Er greift auch für `Default`, den alten Namen des Material-Themes. Eine von älterer Firmware geschriebene `config.json` behält durch diesen Fallback ihr Aussehen.
- **test_config_fields**: die Einstellungstabelle in `main/config/config_fields.cpp`. Keine zwei Zeilen haben denselben Namen. Ein Name lässt sich als POST-Argument und als MQTT-Themensegment verwenden. Jeder Tab hat Einträge. Die Neustart-Flags sind korrekt. Keine zwei Zeilen beanspruchen denselben Speicherort. Jeder Standardwert liegt in seinem Bereich. Farben werden genau aus sechs Hexadezimalziffern geparst, nicht aus lockereren Formaten.
- **test_config_file**: `config.json` selbst. Die ausgelieferten Standardwerte, ein Wert pro Abschnitt, der Speichern und Laden übersteht, eine beschädigte Datei, eine Datei mit fehlenden Abschnitten, Farben als `#rrggbb` oder Zahl sowie die Validierung. Die Testsuite erstellt ein eigenes Verzeichnis unter `$TMPDIR`. Sie kann die Konfiguration eines laufenden Simulators nicht berühren.
- **test_multipart**: der Body-Scanner für Firmware-Uploads in `main/web/multipart.c`. Dies ist der einzige Pfad, der ein Panel unbootbar machen kann. Er parst Bytes, die jemand anderes ausgewählt hat, ohne vorgeschaltete Authentifizierung. Beim ersten Durchlauf fand die Testsuite einen Pufferüberlauf: Die Länge der Multipart-Grenze wurde nirgends geprüft.
- **test_ble_beacon**: die Ankündigungsparser in `main/ble/ble_beacon.cpp`. Die iBeacon- und Eddystone-Layouts stammen von Geräten, die niemand hier geschrieben hat. Fehlerhafte Eingaben sind der Normalfall. Die Testsuite prüft eine gültige Ankündigung, die an jeder möglichen Länge abgeschnitten ist. Beim ersten Durchlauf fand sie zwei Fehler in den eigenen Fixtures und einen in den URL-Zeichenbereichen.
- **test_testif_parse**: der Request-Tokenizer in `main/testif/testif_parse.c`. Er liest Datagramme, die außerhalb des Prozesses zusammengestellt wurden. Entscheidend sind die Fälle, die eine von Hand eingegebene `nc -u`-Zeile erzeugt: ein abschließender Zeilenumbruch, ein doppeltes Leerzeichen, ein nicht geschlossenes Anführungszeichen, ein einzelnes `@` und die beiden Token-Limits.
- **test_touch_cal**: die Touchscreen-Kalibrierung in `main/port/touch_cal.c`. Die Abbildung wird über den gesamten 12-Bit-Eingaberaum gegen eine zweite Kopie des Ausdrucks durchlaufen, der vor der Extraktion in `port_indev.c` stand, in beiden Ausrichtungen und mit beiden Flip-Einstellungen: Beide müssen überall übereinstimmen, was die Extraktion nachweislich zu einer reinen Verschiebung macht. Die Lösung wird durch Hin- und Rückrechnung geprüft: Es wird ein Panel erfunden, seine Abbildung rückwärts ausgeführt, um die Messwerte von vier Eckendrucken zu erzeugen, und die Ergebnisse müssen jeden Pixel wieder bis auf einen an seine ursprüngliche Position setzen. Danach werden alle Ablehnungsfälle geprüft: vier identische Messwerte, ein gespiegeltes Panel, zwei widersprüchliche Drucke an einem Ende, eine zu kurze Messreihe und der größte von der Arithmetik erzeugbare Term. Dies ist die einzige Datei hier, deren Fehler nicht durch Ausführen des Geräts gefunden werden können, denn eine so falsche Kalibrierung, dass man sie bemerkt, hat den Zeiger, der zu ihrer Korrektur nötig wäre, bereits unbrauchbar gemacht.
- **test_backlight_control**: die Dimm-Zustandsmaschine in `main/control/backlight_control.cpp` mit einer Uhr, die der Test manuell verstellt. Das einfache Dimmen bleibt unverändert. Gleiche Helligkeitsstufen verschlucken den Wecktipp nicht. Jede Phase des Übergangs über Schwarz tritt rechtzeitig ein; das Dim-Flag ändert sich nur in der dunklen Phase. Ein Tipp während des Ausblendens kehrt den Übergang ohne Bildschirmwechsel um. Eine während des Übergangs geänderte Helligkeitseinstellung greift nicht mitten im Vorgang.
- **test_beeper_mixer**: die Arithmetik in `main/control/beeper_mixer.c`. Hüllkurvenkurven und -kanten, Sweeps in beide Richtungen und die Aufteilung eines Frames auf Stimmen. Zwei Assertions sind entscheidend: Ein Frame ist eine reine Funktion seiner Argumente, und die ausgelieferten Standardwerte ergeben 63 Duty-Zähler (die Lautstärke, die jeder Signalton dieser Firmware je hatte).
- **test_beeper_seq**: die Arithmetik der Standard-Engine. Dieselben Sweeps, ein Glide, der monoton bleiben muss, ein LFO, der nicht überlaufen darf, ein Tremolo, das die Spitze der Hüllkurve nicht überschreiten darf, und eine Note der Länge null, die übersprungen werden muss.
- **test_ui_beep_chimes** und **test_ui_beep_tunes**: die Klangtabellen, je eine Testsuite pro Engine. Drei Familien mit je achtzehn Klängen. Die Suiten prüfen, dass kein Klang fehlt, dass die Frequenzen im Bereich liegen, in dem ein kleiner Piezo laut ist, und dass kein Klang länger anhält als die Geste, auf die er antwortet. Beide Suiten laufen, obwohl ein Panel nur eine Engine ausliefert: Die nicht ausgewählte Engine ist diejenige, deren Veralten niemand bemerken würde.

- **test_event_parse**: der Leser des Ereignisstreams in `main/openhab/openhab_event_parse.cpp`. Der Zeilentrenner verarbeitet über mehrere Lesevorgänge verteilte `data:`-Zeilen, CRLF-Endungen und eine zu lange Zeile, die nicht gespeichert werden kann. Der Ereignisparser verarbeitet, was openHAB 5 auf `/rest/events` sendet: eine Statusänderung, die Änderung einer Gruppe (benannt nach der Gruppe), eine Messgröße mit `°` sowohl unverändert als auch als `\u`-Escape, `NULL` und `UNDEF`, die `smarthome/`-Wurzel von openHAB 2, den Keepalive, Ereignisse ohne Statusänderung, fehlerhafte Eingaben und einen überlangen Wert.
- **test_json_squeeze**: der Filter in `main/openhab/json_squeeze.c`, der jeden JSON-String mit mehr als 512 Byte abschneidet. Der Status eines Image-Elements ist ein Bild in Base64; openHAB fügt es in die Sitemap-Seite und in jedes Ereignis für das Widget ein. Die Testsuite verarbeitet eine Seite mit einem 60-KB-Status in Lesevorgängen von 1 Byte bis 100 KB und parst das Ergebnis. Außerdem schneidet sie an jeder Position um eine Escape-Sequenz und ein UTF-8-Zeichen herum ab und prüft, dass ein Zeilenumbruch eine neue Zeile des Ereignisstreams beginnt.
- **test_image_decode**: der JPEG-Decoder in `main/openhab/image_decode.c` auf Basis von TJpgDec. Die Fixtures in `test/host/fixtures/` sind vierfarbige Quadranten (rot, grün, blau, weiß) mit 640x480, 800x600 und 1920x1080 Pixeln sowie eine progressive Kopie. Jedes Bild muss innerhalb des angeforderten Bereichs dekodiert werden, wobei die Farben der Quadranten an den richtigen Stellen liegen, und zwar bei Lesevorgängen beliebiger Größe. Die progressive Datei, ein halbierter Stream, `UNDEF` und ein leerer Body müssen fehlschlagen, ohne allozierten Speicher zurückzulassen. Die Testsuite prüft außerdem für alle Größen bis 4000x3000 die Wahl der Verkleinerung und des Pixelschritts.

## Konverter des Visualizers

Der SweetHome3D-Konverter des MQTT-Visualizers (`mqttviz/sh3d.py`) hat eigene Tests, die nur die Standardbibliothek benötigen:

    python3 mqttviz/test_sh3d.py

Sie erstellen `.sh3d`-Dateien im Speicher und prüfen Ebenen, Maßeinheiten und Achsen, schräge und runde Wände, Räume und Öffnungen sowie die Ablehnungsfälle: keine ZIP-Datei, eine alte Datei ohne `Home.xml`, fehlerhaftes XML und ein leeres Zuhause. Die Seite selbst wird visuell geprüft: `mqttviz/demo/` führt sie mit einer erfundenen Geräteflotte aus (siehe [Ohne Panels ausprobieren](mqttviz_de.md#ohne-panels-ausprobieren)).

## Tests mit einem echten openHAB

Die einkompilierten Fixtures des Simulators zeichnen einen Bildschirm ohne Server. Sie können nicht aussagen, was openHAB tatsächlich sendet. `test/openhab/` enthält Sitemaps und Elemente für einen echten Server:

- Eine Seite mit sechs Widgettypen.
- Eine Seite mit Formen, die auf der ersten Seite nicht vorkommen.
- Ein fünfstufiger Navigationsbaum, der jedes feste Limit des Panels erreicht.

`test/openhab/seed-states.sh` weist ihnen allen einen aussagekräftigen Status zu.

Siehe [doc/openhab-fixtures_de.md](openhab-fixtures_de.md) für die Installation, Hinweise zum Übertragungsformat und die Stellen, an denen Panel und Server noch voneinander abweichen.

## Die Steuerschnittstelle

Der Simulator lauscht für Befehle auf 127.0.0.1:8781. Ein Testgerät bietet denselben Kanal im Netzwerk an, wenn es mit `CONFIG_OHEZ_TESTIF` erstellt wurde (die Standardeinstellungen von `arduitouch_jtag` aktivieren es; bei allen anderen Targets bleibt es deaktiviert). `tools/ohez_ctl.py` ist der Client für beide. Damit lassen sich Kacheln anhand ihrer Beschriftung antippen, der Bildschirm als JSON auslesen, die Speicheraufteilung abfragen und Bildschirmfotos abrufen – letzteres nur im Simulator. Siehe [Simulator](simulator_de.md#den-simulator-skriptgesteuert-bedienen) und die vollständige Referenz in [doc/test-interface_de.md](test-interface_de.md).
