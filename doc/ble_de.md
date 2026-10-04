# Bluetooth-LE-Beacons

Standardmäßig deaktiviert. Wenn **Scan for BLE beacons** eingeschaltet ist,
empfängt das Bedienfeld BLE-Ankündigungen und veröffentlicht die empfangenen
Daten unter einem `ble/`-Zweig auf dem MQTT-Broker. Das Bedienfeld empfängt
nur. Es sendet keine Ankündigungen, verbindet sich mit nichts und kann nicht
gekoppelt werden.

Zum Einschalten des Scanners ist ein Neustart erforderlich. Der
Bluetooth-Controller beansprucht beim Start mehrere zehn Kilobyte RAM. Wird er
gestoppt, gibt er den RAM nicht wieder frei. Auf einem Bedienfeld, das nicht
scannt, darf der Controller niemals gestartet worden sein.

## Topics

Die Topics befinden sich unter dem MQTT-Präfix. Mit den Standardwerten lauten
sie `oheztouch/oheztouch-new/ble/...`. Der Schlüssel ist die Hardwareadresse
des Advertisers in kleingeschriebenem Hexadezimalformat ohne Trennzeichen.

| Topic | Veröffentlicht | Wert |
| --- | --- | --- |
| `count` | in jedem Zeitfenster | Anzahl der aktuell veröffentlichten Advertisers |
| `dropped` | in jedem Zeitfenster | Seit dem Start aufgrund einer vollen Warteschlange verlorene Meldungen. Normalerweise 0. |
| `<addr>/type` | bei Erkennung | `iBeacon`, `Eddystone-UID`, `Eddystone-URL` oder `device` |
| `<addr>/id` | bei Erkennung | Die eigene Identität des Beacons oder leer, falls ein Gerät keine hat |
| `<addr>/name` | bei Erkennung | Der angekündigte Name, sofern vorhanden |
| `<addr>/addrtype` | bei Erkennung | `public` oder `random`. Eine zufällige Adresse wechselt und kann nicht nachverfolgt werden. |
| `<addr>/uuids` | bei Erkennung | Die vom Gerät angekündigten Service-UUIDs, kommasepariertes Hexadezimalformat. Was ein Gerät *ist*, steckt meist hier. |
| `<addr>/manufacturer` | bei Erkennung | Herstellerdaten als Hexadezimalwert (zuerst die Unternehmenskennung), sofern das Gerät welche sendet, die nicht zu einem iBeacon gehören |
| `<addr>/service` | bei Erkennung | Servicedaten als Hexadezimalwert (zuerst die UUID), sofern das Gerät welche sendet, die nicht zu Eddystone gehören |
| `<addr>/power` | bei Erkennung | dBm: die vom Beacon angegebene Leistung in einem Meter Entfernung oder die Sendeleistung eines normalen Geräts |
| `<addr>/rssi` | in jedem Zeitfenster | dBm, über das Zeitfenster gemittelt |
| `<addr>/distance` | in jedem Zeitfenster | Geschätzte Entfernung in Metern. Nur bei Beacons. Siehe unten. |
| `<addr>/battery` | in jedem Zeitfenster | mV, nur Eddystone-TLM |
| `<addr>/temperature` | in jedem Zeitfenster | Grad Celsius, nur Eddystone-TLM |

Die Zeilen mit „bei Erkennung“ werden einmal und erneut nur dann
veröffentlicht, wenn sich der Wert ändert. Die Identität eines Advertisers
sind keine Telemetriedaten.

Drei Formate werden erkannt:

- **iBeacon**: Die Identität besteht aus der Proximity-UUID, der Major- und der Minor-Nummer.
- **Eddystone-UID**: ein Namespace und eine Instanz.
- **Eddystone-URL**.

**Eddystone-TLM** ist keine Identität, sondern Telemetrie. Beacons senden sie
zwischen ihren Identitäts-Frames. Die Werte für Akku und Temperatur werden dem
Eintrag hinzugefügt, den die Identitäts-Frames erstellt haben.

Alles andere in Reichweite (ein Smartphone, eine Uhr, ein Thermostat) ist ein
`device`. Es wird mit seinem angekündigten Namen, seiner Sendeleistung, seinen
Service-UUIDs und Herstellerdaten veröffentlicht – und nur, wenn **Publish non-beacon devices** eingeschaltet ist.

## Aktiver Scan

Standardmäßig deaktiviert. Ist die Funktion ausgeschaltet, empfängt das
Bedienfeld passiv: Es hört nur das Advertise-Paket ab. Die meisten Geräte
übertragen alles Interessante – vor allem den Namen – in der *Scan Response*,
die nur an ein Funkgerät gesendet wird, das danach fragt. Bei einem passiven
Scan werden daher meist nur Adressen und RSSI-Werte ohne weitere Daten
veröffentlicht.

Wenn **Active scan (request scan responses)** eingeschaltet ist, fordert
das Bedienfeld die Response von scannbaren Advertisern an und fügt den Namen
und alle weiteren darin enthaltenen Daten dem Eintrag hinzu, den das
Advertise-Paket erstellt hat. Die Anfrage benötigt pro scannbarem Advertiser
und Zeitfenster etwas Sendezeit. Der WiFi-/Bluetooth-Koexistenz-Arbiter nimmt
diese von dem Anteil des openHAB-Verkehrs weg – daher ist die Funktion
standardmäßig deaktiviert. Die Einstellung wird sofort übernommen: Sie wird
am Anfang jedes Scan-Zeitfensters gelesen, sodass eine Änderung im nächsten
Zeitfenster wirksam wird.

Echte iBeacon- und Eddystone-Hardware überträgt ihre Frames direkt im
Advertise-Paket, damit sie auch von ausschließlich passiven Empfängern gelesen
werden können: Beacons benötigen diese Einstellung nicht. Smartphones, Uhren
und Software-Beacons dagegen meistens schon.

Wenn ein Advertiser **Forget after** Sekunden lang nicht empfangen wurde,
werden alle seine Topics mit einer Retained-Veröffentlichung der Länge null
gelöscht. So wird eine Retained-Nachricht entfernt. Andernfalls bliebe ein aus
dem Gebäude mitgenommener Beacon mit seinem letzten RSSI-Wert für immer im
Broker.

## Zwei Dinge, die Sie vor dem Einrichten wissen sollten

**Die Adresse ist der Schlüssel, die Identität ein Wert.** Das ist umgekehrt
zu der üblichen Darstellung. Es muss so sein. Die UUID eines iBeacons wird
absichtlich gemeinsam verwendet: Hundert Tags eines Geschäfts tragen dieselbe
UUID und unterscheiden sich nur in der Minor-Nummer. Die UUID ist nicht
eindeutig. Die Adresse ist immer eindeutig. Ein Beacon, der seine Adresse
zufällig ändert (aus Datenschutzgründen die meisten Smartphones und viele
Tags), taucht alle paar Minuten unter einem neuen Schlüssel auf. Ein Beacon,
der nachverfolgt werden soll, sendet eine stabile Adresse.

**Die Entfernung ist ein Schätzwert und fällt zu gering aus.** Sie wird mit
dem Log-Distance-Pfadverlustmodell und dem Exponenten 2.0 berechnet, der für
den freien Raum gilt. In Innenräumen liegt der Exponent eher bei 3.
Wände und Möbel machen den Schätzwert zu optimistisch. Der Wert wird
veröffentlicht, weil „ungefähr zwei Meter oder ungefähr zwanzig“ nützlich ist,
ein roher RSSI-Wert dagegen nicht. Interpretieren Sie ihn nicht genauer als
das. Die Entfernung wird *nur* für Beacons veröffentlicht. Nur ein Beacon
gibt eine auf eine bekannte Entfernung kalibrierte Leistung an. Die
Sendeleistung eines normalen Geräts sagt aus, wie stark sein Funkgerät sendet,
nicht wie stark es in einem Meter Entfernung ist.

## Ressourcenbedarf

Bluetooth verwendet NimBLE in der Observer-Rolle mit reduzierten Rollen. Es
benötigt etwa 185 KB Flash. Die zeitkritischen Pfade der WiFi-Bibliothek werden
aus dem IRAM verschoben, um Platz für den Controller zu schaffen. Andernfalls
wäre der IRAM zu 98.4 % belegt. Der RAM des Controllers wird beim Start
reserviert, aber nur, wenn die Einstellung aktiviert ist.

Die Verbindungsinfrastruktur ist auf das reduziert, was ein Observer nutzt:
Der Build dimensioniert NimBLE für ein Gerät, das Verbindungen herstellt –
MSYS-Pools, ACL-Transportpuffer, Controller-RAM für drei Verbindungen –
dieses Bedienfeld stellt jedoch keine Verbindungen her. Daher werden diese
Ressourcen in `sdkconfig.defaults.esp32` auf ihre Mindestwerte (zwei Blöcke,
ein Verbindungsplatz) reduziert; das spart etwa 17 KB statisch reservierten
RAM. Die Central-Rolle bleibt einkompiliert: Wird sie entfernt, fehlt ein
Symbol, auf das der Linker weiterhin verweist, und zur Laufzeit kostet die
Rolle nichts.

Das Funkgerät wird gemeinsam mit WiFi genutzt. Software-Koexistenz wechselt
zwischen beiden. Keines wird dadurch angehalten, aber ein Scan beansprucht
während seiner Laufzeit Sendezeit, die sonst dem openHAB-Verkehr und der
Weboberfläche zur Verfügung stünde. Daher findet alle dreißig Sekunden ein
fünfsekündiges Zeitfenster statt, statt kontinuierlich zu scannen. Ein Beacon
sendet mehrmals pro Sekunde. Fünf Sekunden liefern viele Meldungen von allem
in Reichweite.

Zwei Ausnahmen beeinflussen den Arbeitszyklus zusätzlich; beide betreffen den
Heap, den ein geöffnetes Zeitfenster belegt. Das erste Zeitfenster nach dem
Start beginnt erst nach fünfzehn Sekunden, weil beim Laden der Startseite
beim Start alle Symbole der ersten Seite gleichzeitig decodiert werden und
ein darunter geöffnetes Scan-Zeitfenster den dafür nötigen Heap beanspruchen
würde. Und solange jemand das Bedienfeld verwendet, beginnt kein Zeitfenster;
ein geöffnetes Zeitfenster wird vorzeitig beendet. Bei einer Interaktion
decodiert das Bedienfeld Symbole, während eines offenen Zeitfensters benötigt
das Funkgerät den meisten Heap, und durch die Berührung des Bedienfelds ist
die Frage, für die der Scanner gedacht ist, bereits beantwortet. Empfangene
Daten eines verkürzten Zeitfensters werden wie alle anderen gemittelt und
veröffentlicht.

## Testen ohne Hardware

Der Simulator kann die einkompilierten Ankündigungs-Fixtures bereitstellen:
einen iBeacon, einen Eddystone-UID samt TLM-Frame und ein normales Gerät,
dessen Name als Scan Response eintrifft. Dadurch wird der Unterschied durch
die Einstellung für den aktiven Scan sichtbar. Siehe
[Simulator](simulator_de.md#umgebungsvariablen), `OHEZ_BLE_FIXTURE`.
