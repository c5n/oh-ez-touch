# Geräte aktualisieren

Geräte werden drahtlos aktualisiert. `tools/batchupdate.py` aktualisiert ein
oder mehrere Geräte. Benötigt wird nur Python 3.

Ein Update verändert weder Einstellungen noch WLAN-Zugangsdaten. OTA schreibt
nur in die inaktive App-Partition. `config.json` auf der SPIFFS-Partition und
die Zugangsdaten im NVS bleiben unverändert.

Ein Gerät, das von einer Firmware vor Version 0.90 aktualisiert wird, behält
sein Erscheinungsbild und seine Sensoreinstellungen:

- Eine `config.json` ohne Abschnitt `ui` erhält das Classic-Theme.
- Die BME280-Einstellungen werden von ihrem früheren Speicherort unter `openhab.sensors` gelesen.
- Die AutoConnect-WLAN-Zugangsdaten werden beim ersten Start migriert.

## Geräteliste

Die Geräte werden in einer JSON-Listendatei aufgeführt. Jeder Eintrag enthält
das Build-Ziel, das die Hardware benötigt. Das Ziel kann nicht aus der Ferne
ermittelt werden, und ein Gerätebestand besteht üblicherweise aus mehreren
Typen. Das Ziel ist der Name eines Build-Verzeichnisses unter `build/`.

Beispiel `myOhEzTouchDevices.json` (siehe `tools/devices.example.json`):

```json
{
  "devices": [
    { "host": "oheztouch-01", "target": "arduitouch" },
    { "host": "oheztouch-02", "target": "arduitouch28", "comment": "hallway, 2.8 inch" },
    { "host": "192.168.1.50", "target": "lanbon" }
  ]
}
```

Die früheren PlatformIO-Zielnamen (`ArduiTouch`, `ArduiTouch28`, `Lanbon`) aus
Listendateien vor Version 0.90 werden als Aliase akzeptiert.

### Geräte erkennen

`tools/discover.py` durchsucht ein Subnetz nach Geräten und gibt die
gefundenen Geräte tabellarisch aus. Mit `-o` wird das Ergebnis im Format der
Listendatei gespeichert:

```bash
./tools/discover.py 192.168.1.0/24                      # just look
./tools/discover.py 192.168.1.0/24 -o myOhEzTouchDevices.json
```

Geräte mit Version 0.90 oder neuer antworten mit ihrer Version und ihrem Ziel.
Ihre Einträge sind vollständig. Geräte mit älterer Firmware werden anhand
ihrer AutoConnect-Seiten erkannt, können aber weder Version noch Ziel über
HTTP mitteilen. Ihre Einträge enthalten `"target": null`. Das ist beabsichtigt:
`batchupdate.py` lehnt eine solche Datei ab, bis eine Person das jeweilige
Board jedes Geräts ergänzt hat. Das Ziel legt fest, welches Image das Gerät
erhält. Ein Gerät mit Firmware vor Version 0.90 teilt seinen konfigurierten
Hostnamen mit: Der Scanner liest ihn von der `openhab_settings`-Seite des
Geräts in den Eintrag für `"comment"`.

Auch die gespeicherte `"version"` wird verwendet. Ein Gerät, auf dem bereits
die erwartete Version läuft, wird übersprungen. Wird nach einer teilweise
ausgerollten Aktualisierung erneut gescannt, enthält die Liste nur noch die
Geräte, die aktualisiert werden müssen.

## Verwendung

```
Usage:
    ./tools/batchupdate.py myOhEzTouchDevices.json [options]
    ./tools/batchupdate.py -t <target> <hostname1> <hostname2> ...

    -p, --parallel N    update N devices at once (default 1)
    -i, --interactive   ask before every device: yes, skip or abort
    --minimal           flash the board-independent recovery image instead
    --release X.Y       take images from release/X.Y (default: the latest)
    --timeout S         per-device reboot and verify wait (default 120)
    --retries N         upload attempts per device (default 1)
    --dry-run           validate the list, the images and reachability only
    -o, --retry-file F  where failed devices are written (default retry.json)
```

Für jedes Gerät führt das Skript Folgendes aus:

1. Prüft, ob das Firmware-Image vorhanden ist.
2. Prüft, ob das Gerät auf HTTP antwortet.
3. Sendet das Image per POST an `/update`.
4. Wartet, bis das Gerät wieder erreichbar ist.
5. Liest dessen Statusseite. Version und Ziel müssen übereinstimmen.

Schritt 5 ist erforderlich, da Firmware vor Version 0.90 auch dann mit HTTP
200 antwortet, wenn das Schreiben des Flash fehlgeschlagen ist. Ein
erfolgreicher POST beweist nichts. Nur ein Gerät, das mit der erwarteten
Firmware wieder verfügbar ist, gilt als erfolgreich aktualisiert.

Während das Skript läuft, zeigt ein Terminal oberhalb eines Fortschrittsbalkens
für jedes gerade bearbeitete Gerät eine aktuelle Zeile an (Prüfung, Upload mit
Prozentangabe, Warten auf den Neustart, Verifizierung). Wird die Ausgabe in
eine Datei umgeleitet, erscheint jede Zustandsänderung als einfache Zeile.

Fehlgeschlagene Geräte werden im selben JSON-Format in die Retry-Datei
geschrieben. Ein erneuter Lauf erfolgt einfach mit
`./tools/batchupdate.py retry.json`. Der Exit-Code entspricht der Anzahl
fehlgeschlagener Geräte.

## Release-Builds

Ein Befehl baut alle Hardware-Ziele sauber und sammelt die mit der Version
gekennzeichneten Images unter `release/<version>/`:

```bash
./tools/build_release.py
```

Mit `--dirty` bleibt der Build inkrementell und am bisherigen Ort. `-t` baut
eine Teilmenge.

Das Update-Tool verwendet standardmäßig Images aus dem neuesten Release. Mit
`--release X.Y` wird eine ältere Version ausgewählt. Für ein Ziel, das im
Release fehlt, wird auf dessen Build-Verzeichnis unter `build/` zurückgegriffen.
Vor dem Flashen wird die Quelle jedes Images ausgegeben.

## Ablauf der Aktualisierung

1. Prüfen Sie, ob alle Geräte erreichbar und alle Images gebaut sind. Es wird
   noch nichts geflasht:

   ```bash
   ./tools/batchupdate.py --dry-run myOhEzTouchDevices.json
   ```

2. Aktualisieren Sie zunächst ein Gerät pro Zieltyp:

   ```bash
   ./tools/batchupdate.py -t <target> <hostname1> <hostname2> ...
   ```

3. Aktualisieren Sie die übrigen Geräte parallel:

   ```bash
   ./tools/batchupdate.py -p 4 myOhEzTouchDevices.json
   ```

Alternativ können Sie den interaktiven Modus verwenden. Vor dem Senden wird
für jedes Gerät gefragt: „yes“ aktualisiert es, „skip“ lässt es für die
Retry-Datei aus, „abort“ beendet den Lauf.

```bash
./tools/batchupdate.py -i myOhEzTouchDevices.json
```

## Die minimale Wiederherstellungs-Firmware

Ein fünfter Build, `minimal`, ist der OTA-Wiederherstellungspfad. Er ist kein
Bedienfeld. Er hat kein Display, keinen Touchscreen, kein openHAB, kein MQTT,
keine Sensoren und keinen Ton. Er enthält nur WLAN (gespeicherte
Zugangsdaten oder den Setup-Access-Point), eine kleine Statusseite und den
Upload unter `/update`.

Er steuert keine boardspezifischen Komponenten. **Ein Image passt auf jedes
Board.** Mit ungefähr 40 % der Größe des vollständigen Images ist es der
schnelle Weg auf ein Gerät, dessen alte Firmware einen Upload Byte für Byte
in den Flash schreibt. Von dort wird das vollständige Image mit der festen
Firmwaregeschwindigkeit übertragen.

Bauen Sie es wie ein Hardware-Ziel:

```bash
idf.py -B build/minimal -DSDKCONFIG=build/minimal/sdkconfig \
       -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32;sdkconfig.defaults.minimal" \
       set-target esp32
idf.py -B build/minimal build
```

`tools/build_release.py` baut es zusammen mit den Hardware-Zielen.
`tools/batchupdate.py --minimal` flasht es auf jedes ausgewählte Gerät.
Bei der Verifizierung wird der eigene Zielname des Wiederherstellungs-Builds
(`Minimal`) erwartet. Ein Gerät, das mit seiner alten Firmware zurückkommt,
lässt den Lauf weiterhin fehlschlagen.

Der übliche Wiederherstellungsablauf:

1. Flashen Sie das Wiederherstellungs-Image mit `--minimal`.
2. Führen Sie den Befehl ohne `--minimal` erneut aus, um das vollständige Image zu installieren.
