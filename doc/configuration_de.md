# Konfiguration

Ein Gerät kann an drei Stellen konfiguriert werden: auf dem Einstellungsbildschirm des Bedienfelds, in der Weboberfläche und über MQTT. Alle drei lesen dieselbe Einstellungstabelle in `main/config/config_fields.cpp`. Sie können nicht voneinander abweichen.

Mit `*` markierte Einstellungen werden nur beim Start des Geräts gelesen. Sie werden nach einem Neustart wirksam. Alle anderen Einstellungen gelten, sobald sie gespeichert wurden.

## WLAN einrichten

Ein neues Gerät hat keine WLAN-Zugangsdaten. Es öffnet einen offenen Zugangspunkt, der nach seinem Hostnamen benannt ist (`oheztouch-new` standardmäßig). Auf dem Bildschirm werden der Name des Zugangspunkts und die aufzurufende Adresse angezeigt.

1. Verbinden Sie Ihr Telefon mit diesem offenen WLAN.
2. Öffnen Sie `http://192.168.4.1/`. Es erscheint kein Captive-Portal-Popup. Deshalb zeigt das Gerät die Adresse selbst an.
3. Geben Sie im WLAN-Bereich den Namen und das Passwort Ihres Netzwerks ein.
4. Drücken Sie **Verbinden**. Das Gerät verbindet sich sofort erneut. Der Zugangspunkt wird einige Sekunden später geschlossen.

Ein Gerät mit Zugangsdaten, über die es sein Netzwerk nicht erreichen kann, öffnet denselben Zugangspunkt zehn Minuten lang. Danach versucht es weiterhin unauffällig, eine Verbindung herzustellen. Der Zugangspunkt ist ein offenes Netzwerk und der Firmware-Update-Endpunkt ist nicht authentifiziert. Deshalb bleibt er nicht unbegrenzt aktiv.

**Ein Gerät ohne WLAN-Zugangsdaten öffnet beim Start auch selbst den Einstellungsbildschirm**, auf der WLAN-Seite. Die gesamte Einrichtung kann am Bedienfeld vorgenommen werden: suchen, Netzwerk auswählen, Passwort eingeben und dann die Seite verlassen. Ein zweites Gerät und ein Browser sind nicht erforderlich.

> **HINWEIS:** Die WLAN-Station unterstützt WPA2, nicht WPA3. `CONFIG_ESP_WIFI_ENABLE_WPA3_SAE` ist in `sdkconfig.defaults.esp32` deaktiviert. Das spart 37 KB Flash-Speicher. Ein Zugangspunkt im WPA2/WPA3-Mischmodus akzeptiert das Bedienfeld als WPA2-Client. Ein Zugangspunkt, der ausschließlich für WPA3 konfiguriert ist, weist das Bedienfeld zurück. Das äußert sich darin, dass das Bedienfeld seinen eigenen Zugangspunkt öffnet und wiederholt Verbindungsversuche unternimmt. Aktivieren Sie die Option und erstellen Sie die Firmware neu, wenn Sie ein solches Netzwerk verwenden.

## Einstellungen auf dem Bildschirm

Drücken Sie auf die obere Leiste, um den Einstellungsbildschirm zu öffnen. In LCARS ist das die Leiste mit dem Seitennamen und der Uhr; auch das Feld mit dem WLAN-Symbol auf der linken Seitenleiste öffnet ihn. Der Bildschirm ist ein Menü aus großen Feldern. Jedes Feld hat ein Piktogramm und einen Namen. Drücken Sie auf ein Feld, um den entsprechenden Abschnitt zu öffnen. Die Leiste oben auf jeder Seite führt zurück. Im ersten Menü schließt **X** den Bildschirm. Die verfügbaren Aktionen einer Seite (**Suchen**, falls zutreffend, **Test** auf der Audio-Seite) befinden sich in einer Leiste am unteren Rand.

| Seite | Inhalt |
| --- | --- |
| Erscheinungsbild (Augensymbol) | Theme-Familie, Nachtvariante und deren Zeitplan, Helligkeitsstufen und Dimm-Zeitlimit |
| Audio (Lautsprechersymbol) | Der Signalgeber: Ein-/Ausschalter, Lautstärkeregler und eine Schaltfläche **Test**, die den Startton des Themes mit der gerade bearbeiteten Lautstärke abspielt |
| System (Zahnradsymbol) | Ein Menü mit den sieben folgenden Abschnitten. Hinter der System-PIN, falls eine eingerichtet ist: siehe [PINs](#pins). |
| &nbsp;&nbsp;WLAN | Netzwerk und Passwort sowie eine Schaltfläche **Suchen**, die die Zugangspunkte in Reichweite mit ihrer Signalstärke auflistet. Drücken Sie auf einen Eintrag, um dessen Namen zu übernehmen. Beim Verlassen der Seite werden die Zugangsdaten gespeichert und die Verbindung wird erneut hergestellt. |
| &nbsp;&nbsp;openHAB (Haussymbol) | Zwei Listen: die openHAB-Server im Netzwerk und die Sitemaps des ausgewählten Servers. Drücken Sie auf einen Eintrag, um ihn auszuwählen. **Suchen** fragt erneut ab. **Manuell** öffnet eine Seite mit Feldern für Host, Port und Sitemap. |
| &nbsp;&nbsp;MQTT (Uploadsymbol) | Broker, Port, Zugangsdaten und zu veröffentlichende Daten. Siehe [MQTT](mqtt_de.md). |
| &nbsp;&nbsp;Sensoren (Standortsymbol) | BME280-Zeilen und BLE-Beacon-Scanner. |
| &nbsp;&nbsp;Gerät (Stiftsymbol) | Der Hostname. Er ist auch der Name des Einrichtungszugangspunkts. Über **PINs** in der unteren Leiste können die beiden PINs eingerichtet, geändert und entfernt werden. |
| &nbsp;&nbsp;Touch (Tastatursymbol) | Die vier Touchscreen-Kalibrierungswerte und eine Schaltfläche **Kalibrieren**. Siehe [Touchscreen kalibrieren](#touchscreen-kalibrieren). |
| &nbsp;&nbsp;Uhrzeit (Sync-Symbol) | NTP-Host, GMT-Versatz und Sommerzeit. |
| Info (Listensymbol) | Systeminformationen, ein Thema pro Seite -- Firmware, Netzwerkadressen, Funkverbindung, Sensoren und Relais -- durch die Pfeile in der unteren Leiste durchblätterbar. |

Jede Einstellung ist eine Zeile mit dem Namen links und der Bedienelement daneben: ein Ein-/Ausschalter für Einstellungen mit zwei Zuständen, ein Schieberegler für Werte innerhalb eines passenden Bereichs (Lautstärke, Helligkeiten, Nachtstunden) und eine Schaltfläche mit dem aktuellen Wert für alles andere. Drücken Sie auf eine Werteschaltfläche, um eine Bildschirmtastatur zu öffnen oder eine Option durch die verfügbaren Werte zu schalten. Ein Abschnitt, dessen Zeilen nicht auf den Bildschirm passen, wird in Seiten mit eigenen logischen Gruppen aufgeteilt, die über die Pfeile in der unteren Leiste durchgeblättert werden; es gibt kein Scrollen.

**Beim Verlassen einer Seite wird sie gespeichert.** Es gibt keine Speichern-Schaltfläche: Ein geänderter Wert wird gespeichert und, sofern möglich, sofort angewendet, wenn Sie die Seite verlassen -- über die Zurück-Leiste, durch Öffnen eines anderen Abschnitts oder durch Schließen des Bildschirms.

Wenn eine geänderte Einstellung zu den wenigen gehört, die erst beim Start gelesen werden (Hostname, BME280 und BLE ein/aus, Ausrichtung), fragt das Bedienfeld nach dem Verlassen der Seite, ob es neu gestartet werden soll.

## PINs

Das Bedienfeld hat zwei PINs, und beide sind optional. Bei einem neuen Gerät ist keine von beiden eingerichtet.

| PIN | Geschützter Bereich |
| --- | --- |
| System-PIN | Der Eintrag **System** auf dem Einstellungsbildschirm und damit WLAN, openHAB, MQTT, Sensoren, Gerät, Touch und Uhrzeit. Theme, Audio und Info bleiben zugänglich. |
| Element-PIN | Sitemap-Kacheln, deren openHAB-Element das Tag `ohez-pin` hat |

Die beiden PINs sind absichtlich getrennt. Wer ein geschütztes Licht schalten darf, muss nicht unbedingt auch das Bedienfeld neu konfigurieren dürfen. Eine PIN besteht aus 4 bis 8 Ziffern. Das Bedienfeld lässt nicht zu, dass beide PINs denselben Wert haben, da dann die Kenntnis der einen auch die Kenntnis der anderen bedeuten würde.

**PINs einrichten.** Öffnen Sie System › Gerät und drücken Sie in der unteren Leiste auf **PINs**. Für jede PIN gibt es **Einrichten** oder, wenn sie bereits existiert, **Ändern** und **Entfernen**. Eine neue PIN wird zweimal eingegeben. In der Zeile der Element-PIN steht außerdem, wie viele Kacheln auf der Seite unter den Einstellungen das Tag tragen. So lässt sich schnell überprüfen, ob das Tag in openHAB übernommen wurde.

**PINs verwenden.** Wenn Sie etwas Geschütztes antippen, erscheint ein Tastenfeld. Nach Eingabe der richtigen PIN wird die Aktion ausgeführt. Die PIN bleibt dann freigeschaltet, bis das Bedienfeld inaktiv wird: Die Hintergrundbeleuchtung dimmt oder eine Minute lang erfolgt keine Berührung -- je nachdem, was zuerst eintritt. Eine zu diesem Zeitpunkt geöffnete Systemseite kehrt zum Einstellungsmenü zurück, und ihre Änderungen werden wie beim Verlassen einer beliebigen Seite gespeichert. Die beiden PINs schalten getrennt frei: Die Element-PIN entsperrt markierte Kacheln und nichts anderes.

Fünf falsche Eingaben sperren das Tastenfeld 30 Sekunden lang. Bei jeweils weiteren fünf falschen Eingaben verdoppelt sich die Wartezeit bis auf maximal fünf Minuten. Der Zähler liegt im Speicher und wird daher bei einem Neustart zurückgesetzt.

**Ein Element schützen.** Fügen Sie das Tag in openHAB hinzu, zum Beispiel in einer `.items`-Datei:

```
Switch Garage_Door "Garage door" ["ohez-pin"]
```

Das Tag muss exakt übereinstimmen und unterscheidet Groß- und Kleinschreibung. Es schützt die Aktion, die durch Antippen der Kachel ausgeführt wird: das Umschalten eines Schalters, den Steuerungsbildschirm eines Schiebereglers oder Farbwählers sowie bei einer Gruppe oder einem Text-Widget mit Element die geöffnete Unterseite. Eine einfache Unterseite `Text label="..." { ... }` hat kein Element und kann daher kein Tag tragen. Schreibgeschützte Kacheln (Zahlen und Zeichenketten) werden nie gesperrt, da ein Antippen nichts bewirkt.

**PIN vergessen?** Entfernen Sie sie über die Weboberfläche: Auf der Seite `/` gibt es bei eingerichteter PIN eine Schaltfläche **System-PIN entfernen** oder **Element-PIN entfernen**; `POST /api/pin` erledigt dasselbe (siehe [REST-API](#rest-api)). Die PINs werden in NVS als gesalzene SHA-256-Hashes und niemals als Ziffern gespeichert. Sie bleiben bei einem Firmware-Flash und einem Update erhalten und erscheinen niemals in `/api/config`, MQTT oder der Einstellungsdatei.

> **HINWEIS:** Die PINs halten Personen vom Bedienfeld fern, nicht vom Netzwerk. Die Weboberfläche ist nicht authentifiziert (siehe Warnung unter [REST-API](#rest-api)), und jeder, der darauf zugreifen kann, kann eine PIN entfernen.

## Touchscreen kalibrieren

Die resistiven Panels unterscheiden sich von Gerät zu Gerät. Die Firmware wird mit den Konstanten ausgeliefert, auf die sich die veröffentlichten Beispiele für das Board einigen. Wenn Ihr Panel an den Rändern um einige Pixel abweicht, kalibrieren Sie es.

Öffnen Sie **System** und dann **Touch** und drücken Sie auf **Kalibrieren**. Auf dem Bildschirm erscheint ein Kreuz in einer Ecke. Drücken Sie auf dessen Mitte. Das wiederholt sich insgesamt viermal.

Drücken Sie jeweils auf die Mitte des angezeigten Kreuzes und nicht auf die Stelle, an der das Panel Ihrer Meinung nach die Berührung erfassen wird. Während des Vorgangs ist weiterhin die falsche Kalibrierung aktiv. Der Bildschirm verwendet weder die gemeldete Position noch die Position des Kreuzes; stattdessen verwendet er den Rohwert und die Stelle, an der das Kreuz gezeichnet wurde. Deshalb funktioniert das Verfahren auch auf einem Panel, das so stark abweicht, dass es sich sonst nicht normal bedienen lässt.

Nach dem vierten Kreuz zeigt der Bildschirm das Ergebnis an.

- Ein Satz nennt den größten Fehler der alten Kalibrierung an den vier gedrückten Stellen und den größten verbleibenden Fehler der neuen Kalibrierung.
- Ein Diagramm zeigt das Panel. Ein Kreuz markiert jede Stelle, an der Sie drücken sollten. Ein Punkt markiert, wohin die alte Kalibrierung Ihren Finger gesetzt hat. Die Verbindungslinie zeigt den Fehler. Die Punkte sind in Originalgröße versetzt auf einem verkleinert dargestellten Panel eingezeichnet, damit Fehler von wenigen Pixeln sichtbar sind.
- Eine Tabelle zeigt die vier Konstanten: den alten Wert, den neuen Wert und die Differenz.

**Übernehmen** speichert die neue Kalibrierung, wendet sie sofort an und schreibt sie in die Datei. **Erneut versuchen** startet den Vorgang neu. **Verwerfen** ändert nichts.

Eine Berührung, die um mehr als zehn Pixel verrutscht, wird nicht gezählt. Drücken Sie erneut, wenn ein Kreuz nicht weitergeschaltet wird: Eine zweite Berührung an derselben Stelle wie die letzte wird verworfen, statt doppelt gezählt zu werden. Erneutes Drücken kann eine Messung also nicht verfälschen.

Das Verfahren lehnt ein Ergebnis ab, dem es nicht vertrauen kann, und nennt den Grund. In diesem Fall wird nichts gespeichert:

| Meldung | Bedeutung |
| --- | --- |
| Berührungen zu nah beieinander | Die vier Berührungen haben nicht den gesamten Bildschirm abgedeckt. |
| Zwei Berührungen stimmen nicht überein. Erneut versuchen | Zwei Berührungen am selben Ende einer Achse ergeben sehr unterschiedliche Werte. Eine davon ist an einer anderen Stelle gelandet. |
| Panel ist gespiegelt. Siehe board_pins.h | Eine Achse verläuft rückwärts. So ist das Panel verdrahtet; es handelt sich nicht um eine Kalibrierung. Verwenden Sie `OHEZ_TOUCH_FLIP` in `main/port/esp32/board_pins.h`. |
| Messwerte außerhalb des Bereichs | Das Panel hat keine brauchbaren Werte gemeldet. |

Das kapazitive Panel des Lanbon L8 meldet das Pixelraster, auf das es geklebt ist. Es muss nicht kalibriert werden und die Schaltfläche **Kalibrieren** wird nicht angezeigt.

### Wenn das Panel so stark abweicht, dass die Schaltfläche nicht erreichbar ist

Die vier Werte sind gewöhnliche Einstellungen. Sie befinden sich im Webformular, in `GET/POST /api/config` unter `touch_x_org`, `touch_x_span`, `touch_y_org` und `touch_y_span` sowie auf dem MQTT-Konfigurationsthema. Setzen Sie alle vier auf `0`, um zu den Konstanten zurückzukehren, mit denen die Firmware erstellt wurde.

## Weboberfläche

Öffnen Sie `http://<hostname>/`. Alles befindet sich auf dieser einen Seite: ein Statusblock, der WLAN-Bereich, alle Einstellungen und Schaltflächen für das Firmware-Update und einen Neustart.

| Route | Zweck |
| --- | --- |
| `/` | Status, WLAN-Bereich und alle Einstellungen |
| `/save` | Speichert die Einstellungen und leitet zurück zu `/` |
| `/wifi` | Speichert die WLAN-Zugangsdaten und stellt die Verbindung erneut her |
| `/pin/clear` | Entfernt eine PIN (`scope=system` oder `scope=item`) und leitet zurück zu `/` |
| `/restart` | Startet das Gerät neu |
| `/update` | Firmware-Upload, auch von `tools/batchupdate.py` verwendet |

### REST-API

Seit Version 0.91 gibt es außerdem eine REST-API. Sie ist für Werkzeuge gedacht. Der [Gerätemanager](devmgr_de.md) ist ihr erster Client.

| Route | Zweck |
| --- | --- |
| `GET /api/status` | Version, Ziel, Betriebszeit, Netzwerk und Heap als JSON. Keine Nebenwirkungen (kein Sitemap-Abruf, kein mDNS-Scan). Kann gefahrlos regelmäßig abgefragt werden. |
| `GET /api/config` | Jede Einstellung mit Bezeichnung, Typ, Wert und Wertebereich oder Optionen als JSON. Geheimnisse werden als `***` maskiert. |
| `POST /api/config` | Ein JSON-Objekt mit geänderten Einstellungen. Nicht angegebene Felder bleiben unverändert. Ein abgelehnter Wert macht die gesamte Anfrage rückgängig. |
| `GET /api/sounds` | Das Klangverzeichnis des aktiven Themes. |
| `POST /api/sound` | `{"name":"door_chime","force":true}` spielt einen Klang ab. `force` spielt ihn trotz stummgeschaltetem Signalgeber ab, damit ein Bedienfeld gefunden werden kann. |
| `GET /api/pin` | `{"system":{"set":true},"item":{"set":false}}`: Gibt an, ob die jeweilige PIN eingerichtet ist. Niemals die PIN selbst. |
| `POST /api/pin` | `{"clear":"system"}` oder `{"clear":"item"}` entfernt die entsprechende PIN. Es gibt keine Möglichkeit, eine PIN über das Netzwerk einzurichten; das geschieht am Bedienfeld. |

> **WARNUNG:** Keine dieser Routen ist authentifiziert. Der Einrichtungszugangspunkt ist offen. Jeder, der das Gerät erreichen kann, kann es neu konfigurieren oder flashen. Verwenden Sie das Gerät nur in vertrauenswürdigen Netzwerken.

### Gerätemanager

`devmgr/` ist ein webbasierter Flottenmanager. Er durchsucht ein Subnetz nach Geräten, aktualisiert sie in festgelegten Abständen, bearbeitet Einstellungen für ein einzelnes Gerät oder eine Auswahl, aktualisiert Firmware mit Fortschrittsanzeige und spielt den Türklingelton eines Bedienfelds ab, um es vor Ort zu finden. Er benötigt nur Python 3:

```bash
python3 devmgr/devmgr.py            # then open http://localhost:8088
```

Die vollständige Beschreibung finden Sie unter [doc/devmgr.md](devmgr_de.md).

## Einstellungsreferenz

### Gerät

| Einstellung | Standardwert | Beschreibung |
| --- | --- | --- |
| Hostname `*` | oheztouch-new | Der Hostname dieses Geräts. Auch der Name des Einrichtungszugangspunkts. |

### Touchpanel

Rohe ADC-Zählwerte. `0` bedeutet die Konstanten, mit denen dieses Board erstellt wurde und mit denen jedes Panel startet. Der zweite Wert jedes Wertepaares ist eine **Spanne**: der Rohabstand über den gesamten Bildschirm, nicht der Messwert an dessen entferntem Rand. Siehe [Touchscreen kalibrieren](#touchscreen-kalibrieren).

| Einstellung | Standardwert | Beschreibung |
| --- | --- | --- |
| X-Ursprung (roh) | 0 | Messwert an einem Rand der X-Achse des Bildschirms. |
| X-Spanne (roh) | 0 | Rohabstand über die X-Achse des Bildschirms. |
| Y-Ursprung (roh) | 0 | Messwert an einem Rand der Y-Achse des Bildschirms. |
| Y-Spanne (roh) | 0 | Rohabstand über die Y-Achse des Bildschirms. |

### NTP-Uhrzeit

| Einstellung | Standardwert | Beschreibung |
| --- | --- | --- |
| Host | pool.ntp.org | Host, der die Uhrzeit bereitstellt, z. B. pool.ntp.org oder Ihr Router. |
| GMT-Versatz | 1 | Versatz Ihrer Zeitzone zur Greenwich Mean Time. |
| Sommerzeit | 1 | Ein: Die EU-Regelung schaltet automatisch um -- eine Stunde vor von 01:00 UTC am letzten Sonntag im März bis 01:00 UTC am letzten Sonntag im Oktober. Aus für eine Zeitzone ohne Sommerzeit. |

### Darstellung

| Einstellung | Standardwert | Beschreibung |
| --- | --- | --- |
| Theme | Material | Erscheinungsbild der Benutzeroberfläche: `Material`, `LCARS`, `JARVIS` oder `Classic`. |
| Nachtmodus | off | `off`, `on` oder `auto`, um der Uhrzeit zu folgen. |
| Ausrichtung `*` | landscape | `landscape` ist das bisherige Format 320x240 aller Panels. `portrait` montiert das Panel aufrecht im Format 240x320: Alle vier Themes ordnen das Layout neu an, die sechs Kacheln werden zu zwei Spalten mit je drei Kacheln. |
| Nacht ab | 22 | Stunde, zu der die Nachtvariante beginnt, wenn der Nachtmodus `auto` ist. |
| Nacht bis | 6 | Stunde, zu der die Nachtvariante endet, wenn der Nachtmodus `auto` ist. |

Das Theme wird sofort nach dem Speichern auf dem Bildschirm und im Browser wirksam. `auto` benötigt die Uhr. Es funktioniert, sobald NTP geantwortet hat.

`Classic` ist das blau-silberne Erscheinungsbild der Original-Firmware. `Material` hieß früher `Default`. Nur der Name wurde geändert. Ein Panel, das von einer älteren Firmware aktualisiert wird, behält sein Erscheinungsbild: Ein unbekannter Theme-Name wählt Material aus. Alles, was den Namen *schreibt* (ein MQTT-`config/theme`-Payload, ein `OHEZ_THEME` in einem Skript), sollte den neuen Namen verwenden. Die alte Schreibweise funktioniert nur als Fallback.

### LCD-Hintergrundbeleuchtung dimmen

| Einstellung | Standardwert | Beschreibung |
| --- | --- | --- |
| Inaktivitätszeitlimit | 60 | Sekunden seit der letzten Berührung, bis das Display gedimmt wird. |
| Normale Helligkeit | 100 | Normale Helligkeit in Prozent. |
| Gedimmte Helligkeit | 40 | Gedimmte Helligkeit in Prozent. |
| Uhrzeit und Datum im gedimmten Zustand anzeigen | Off | Uhrbildschirm beim Dimmen des Displays anzeigen: die Uhrzeit und darunter den Wochentag und das Datum oder die Uhr-Elemente der Startseite. Die nächste Berührung aktiviert das Bedienfeld auf der Startseite. |
| Uhrtext (Tag) | `#ffffff` | Textfarbe des Uhrbildschirms, solange die Tagvariante des Themes aktiv ist. |
| Uhrhintergrund (Tag) | `#000000` | Hintergrundfarbe am Tag. |
| Uhrtext (Nacht) | `#ffffff` | Textfarbe, solange die Nachtvariante aktiv ist (siehe Nachtmodus). |
| Uhrhintergrund (Nacht) | `#000000` | Hintergrundfarbe bei Nacht. |
| Rahmen für Uhrelemente | `Clock` | Bezeichnung des `Frame` auf der Sitemap-Startseite, dessen Elemente der Uhrbildschirm anzeigt. Leer bedeutet keine Anzeige. |

Wenn das Inaktivitätszeitlimit abläuft, kehrt das Bedienfeld zur Sitemap-Startseite zurück. Das geschieht unabhängig davon, ob der Uhrbildschirm aktiviert ist. Ein geöffnetes Elementfenster wird geschlossen. Der Einstellungsbildschirm bleibt unverändert.

Bei aktiviertem Uhrbildschirm verläuft das Dimmen über Schwarz. Die Hintergrundbeleuchtung wird innerhalb von 1,5 s ausgeblendet, der Uhrbildschirm erscheint im Dunkeln und die Hintergrundbeleuchtung wird auf die gedimmte Stufe eingeblendet. Eine Berührung kehrt den Vorgang schnell um: 0,1 s Abdunklung auf Schwarz, dann die Seite und anschließend die normale Helligkeit. Die aufweckende Berührung wird nie an eine Kachel weitergegeben. Eine Berührung während des langsamen Ausblendens kehrt den Vorgang direkt um. Dabei wird nichts ausgetauscht.

**Farben** haben überall das Format `#rrggbb`: in `config.json`, im Farbwähler des Webformulars, in `/api/config` und in MQTT. Der Einstellungsbildschirm des Bedienfelds bietet eine Palette aus zwölf benannten Farben. Ein anderswo eingestellter Wert, der nicht dazugehört, wird als „Benutzerdefiniert“ angezeigt. Die Tag- und Nacht-Farbpaare folgen der aktiven Theme-Variante. Bei Nachtmodus `auto` ändert sich der Uhrbildschirm also entsprechend dem Zeitplan.

**Uhrelemente.** Unter der Uhrzeit zeigt der Uhrbildschirm den Wochentag und das Datum. Wenn die Startseite einen `Frame` mit der konfigurierten Bezeichnung enthält, werden stattdessen die ersten drei Elemente dieses Rahmens angezeigt: Symbol, Wert und Name. Sie werden nicht als Kacheln auf der Seite dargestellt. Symbole aus dem integrierten Satz übernehmen die Textfarbe. Von openHAB bereitgestellte Symbole behalten ihre eigenen Farben.

```
sitemap home label="Home" {
    Frame label="Clock" {
        Text item=Outside_Temperature label="Outside" icon="temperature"
        Text item=Living_Humidity     label="Humidity" icon="humidity"
        Switch item=gLights           label="Lights"   icon="light"
    }
    // ... the tiles
}
```

Jeder andere `Frame` wird als Überschrift behandelt: Seine Elemente werden der Reihe nach zu Kacheln der Seite. Siehe [Der Uhren-Frame](sitemap_de.md#der-uhren-frame) für die Regeln.

### Signalgeber

| Einstellung | Standardwert | Beschreibung |
| --- | --- | --- |
| Signalgeber aktivieren | On | Signaltöne aktivieren. |
| Lautstärke | 25 | 0 bis 100. 0 bedeutet lautlos. |

Die Lautstärke ist der PWM-Tastgrad. Bei einem Piezoelement entspricht der Tastgrad nur ungefähr der Lautstärke. Das Signal ist eine Rechteckwelle. Ihre Grundschwingung entspricht `sin(pi x duty)`. 100 entspricht dem Tastgrad von 50 %, dem lautesten möglichen Impulszug. Der Standardwert 25 entspricht dem Tastgrad, den jeder Signalton dieser Firmware bisher verwendet hat. Siehe den Dateikommentar in `main/port/esp32/port_beeper.c`.

Das Bedienfeld hat ein Piezoelement an einem GPIO und einen LEDC-Timer. Es kann immer nur ein Ton gleichzeitig abgespielt werden. Zwei Engines nutzen ihn unterschiedlich. Die Standard-Engine spielt eine einzelne Note mit Hüllkurve, Gleitton und Verzierung. Die andere Engine verschachtelt bis zu drei Stimmen im Abstand von jeweils zwei Millisekunden, um einen echten Akkord zu erzeugen. Welche Engine einkompiliert wird, ist eine menuconfig-Auswahl: `CONFIG_OHEZ_BEEPER_ENGINE`. Siehe [Der Signalgeber](beeper_de.md).

**Der Lanbon L8 hat keinen Summer.** Beide Einstellungen haben dort keine Wirkung. Der Simulator verfügt über einen Signalgeber. Siehe [Das Panel hören](simulator_de.md#das-panel-hören).

Die Schaltfläche **Demo** auf dieser Seite spielt ein 30 Sekunden langes Stück ab. Es durchläuft alle Hüllkurven, Effekte, beide Tonläufe, die Wiederholungszahl und den gesamten Lautstärkebereich. Dabei wird die gerade bearbeitete Lautstärke verwendet. Während der Wiedergabe wird die Schaltfläche zu **Stopp**. Die Schaltfläche gibt es nur in einem Build mit der Standard-Engine.

### openHAB-Server

| Einstellung | Standardwert | Beschreibung |
| --- | --- | --- |
| Host | openhabian | Hostname des openHAB-Servers. |
| Port | 8080 | Port. |
| Sitemap | oheztouch | Name der Sitemap für dieses Gerät. |

Host und Port müssen nicht nachgeschlagen werden. openHAB kündigt sich im lokalen Netzwerk über mDNS (`_openhab-server._tcp`) an. Beim Laden dieser Seite wird die Abfrage gesendet. Jeder antwortende Server erscheint unter dem Host-Feld als Schaltfläche. Die Schaltfläche übernimmt Host und Port. Auf dem Bedienfeld werden dieselben Server als Liste angezeigt. Gespeichert wird die Adresse in Ziffern. Das Bedienfeld kann den vom Server angegebenen `.local`-Namen nicht auflösen.

Die Erkennung findet nur statt, was sich selbst ankündigt. Ein Zugangspunkt, der Multicast filtert, oder ein openHAB in einem Docker-Bridge-Netzwerk wird nicht erkannt. Dafür gibt es **Manuell**.

Auch die Sitemap muss nicht aus dem Gedächtnis eingegeben werden. Beim Laden dieser Seite wird der ausgewählte Server abgefragt (`GET /rest/sitemaps`). Das Feld bietet die Sitemaps als Dropdown-Liste an. Das Bedienfeld kann zwölf Sitemaps speichern und weist darauf hin, wenn es mehr gibt.

Elementzustände erreichen das Bedienfeld ohne Abfragen. Das Bedienfeld hält `GET /rest/events` für die auf dem Bildschirm angezeigten Elemente offen, und openHAB sendet jede Änderung sofort. Das Bedienfeld fordert kein Token an, daher muss dieser Endpunkt wie die Sitemap und die Elementzustände für anonyme Clients zugänglich sein. Das ist die openHAB-Standardeinstellung („implizite Benutzerrolle“). Solange der Stream nicht verfügbar ist, fragt das Bedienfeld alle fünf Sekunden jede Kachel ab. Wenn der Stream verfügbar ist, prüft das Bedienfeld weiterhin jede Kachel einmal pro Minute und fünf Sekunden nach einer Bedienung. In beiden Fällen muss nichts konfiguriert werden.

Auf dem Bedienfeld wird die Sitemap-Liste vom Host und Port abgerufen, während diese bearbeitet werden. Das Webformular fragt den gespeicherten Endpunkt ab. Dort ist die Reihenfolge: Server auswählen, **Speichern**, dann eine Sitemap von der neu geladenen Seite auswählen.

### Türklingel

| Einstellung | Standardwert | Beschreibung |
| --- | --- | --- |
| Klingel-Element | | Das Element, das beim Drücken der Klingel auf ON (ein Switch) oder OPEN (ein Contact) wechselt. Leer schaltet die Türklingel aus. |
| Bild-Element | | Das Image-Element mit dem Schnappschuss der Türklingel. |
| Bild anzeigen für | 30 | Sekunden, bis das Bild wieder ausgeblendet wird. Eine Berührung lässt es weiter anzeigen. 0 zeigt es bis zum Zurück-Balken an. |

Auf dem Bedienfeld befinden sich diese drei Einstellungen hinter **Manuell** auf der openHAB-Seite, nach den Serverfeldern.

Wenn sich das Klingel-Element zu ON oder OPEN ändert, wird das Bedienfeld aktiviert, spielt einen Klingelton ab und zeigt das Bild-Element über dem aktuellen Bildschirminhalt an. Bei einem weiteren Klingeln während der Anzeige wird das Bild erneut abgerufen. Das Klingel-Element muss sich nicht auf einer Sitemap-Seite befinden: Solange beide Namen eingestellt sind, hält das Bedienfeld für dieses Element ein zweites `GET /rest/events` offen. Das benötigt etwa 8 KB RAM.

Viele Türklingel-Bindings melden einen Tastendruck auf einem Trigger-Kanal und nicht auf einem Element. Wandeln Sie das Signal mit einer Regel in einen Switch um oder verknüpfen Sie den Kanal mit einem Switch-Element, das über ein Profil auf ON gesetzt wird, und schalten Sie es nach einigen Sekunden wieder auf OFF (zum Beispiel mit `expire="5s,command=OFF"` in den Metadaten des Elements). Nur eine Änderung löst das Klingeln aus, ein Element, das auf ON bleibt, klingelt also nur einmal.

Wie das Bild abgerufen und dargestellt wird, ist unter [Bild](sitemap_de.md#bild-eine-türklingel-oder-eine-kamera) beschrieben. Die Demo enthält eine Türklingel zum Ausprobieren: siehe [die Door-Seite](openHAB/README.md#door-a-doorbells-picture).

### MQTT-Broker

| Einstellung | Standardwert | Beschreibung |
| --- | --- | --- |
| MQTT aktivieren | off | Mit dem unten angegebenen Broker verbinden und dort veröffentlichen. |
| Host | mosquitto | Hostname des MQTT-Brokers. |
| Port | 1883 | Port. Nur reines TCP. TLS wird nicht unterstützt. |
| Benutzer | | Für einen anonymen Broker leer lassen. |
| Passwort | | Wird zusammen mit dem Benutzernamen gesendet. Wird niemals im Klartext angezeigt oder veröffentlicht. |

### MQTT-Veröffentlichung

| Einstellung | Standardwert | Beschreibung |
| --- | --- | --- |
| Basis-Topic | oheztouch | Erstes Segment jedes Topics. Danach folgt der Hostname. |
| Veröffentlichungsintervall | 60 | Sekunden zwischen zwei Runden mit Systeminformationen. |
| Veröffentlichte Werte beibehalten | On | Mit gesetztem Retain-Flag veröffentlichen. Ein später verbundener Subscriber sieht die aktuellen Werte sofort. |

Die MQTT-Einstellungen werden sofort nach dem Speichern wirksam. Der Client verbindet sich selbst erneut, und zwar nur, wenn sich eine von ihm verwendete Einstellung geändert hat.

### Sensoren

| Einstellung | Standardwert | Beschreibung |
| --- | --- | --- |
| BME280-Sensor verwenden `*` | off | Den optionalen BME280 auslesen und die Werte über MQTT veröffentlichen. |
| Aktualisierungsintervall | 180 | Sekunden zwischen zwei Messungen. |

Ein Messwert wird an den Broker und sonst nirgendwo gesendet. Siehe [Veröffentlichte Topics](mqtt_de.md#veröffentlichte-topics). Eine openHAB-Installation, die die Messwerte benötigt, abonniert die drei Topics über ihr eigenes MQTT-Binding.

### Bluetooth-LE-Beacons

| Einstellung | Standardwert | Beschreibung |
| --- | --- | --- |
| Nach BLE-Beacons suchen `*` | off | Auf BLE-Ankündigungen hören und sie über MQTT veröffentlichen. Siehe [Bluetooth-LE-Beacons](ble_de.md). |
| Suche alle | 30 | Sekunden zwischen den Starts zweier Scanfenster. |
| Suchdauer | 5 | Sekunden pro Fenster. Keine kontinuierliche Suche, da das Funkmodul mit WLAN geteilt wird. |
| Aktive Suche | off | Abfragbare Sender um ihre Scan-Antwort bitten, in der ein Telefon oder eine Uhr ihren Namen und die meisten ihrer Daten übermittelt. Standardmäßig aus: Die Anfrage benötigt Sendezeit, die dem WLAN-Anteil abgeht. Wird zu Beginn jedes Fensters gelesen; eine Änderung wird daher ohne Neustart wirksam. |
| Schwächere Signale ignorieren als | -90 | Ankündigungen unterhalb dieses RSSI-Werts werden verworfen. |
| Vergessen nach | 120 | Sekunden ohne Signal, bevor ein Beacon entfernt und seine Topics gelöscht werden. |
| Nicht-Beacon-Geräte veröffentlichen | off | Auch gewöhnliche BLE-Geräte veröffentlichen, nicht nur erkannte Beacons. |
