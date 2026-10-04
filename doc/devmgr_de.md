# Der Gerätemanager (devmgr/)

Ein webbasiertes Verwaltungstool für eine Flotte von OhEzTouch-Panels: ein Python-Skript, eine Seite auf localhost, nichts außer Python 3 muss installiert sein. Es findet Geräte, überwacht sie, konfiguriert sie einzeln oder in Gruppen, aktualisiert ihre Firmware mit Fortschrittsanzeige und spielt den Türgong eines Panels ab, wenn du dessen Standort vergessen hast.

    python3 devmgr/devmgr.py            # then open http://localhost:8088

Die Geräte werden über die REST-API der Firmware verwaltet und müssen daher **Version 0.91 oder neuer** ausführen. Ältere Firmware bietet keine verwaltbare API; aktualisiere sie zuerst, wie es `tools/batchupdate.py` schon immer getan hat.

| Option          | Standard | Bedeutung                                              |
| --------------- | ------- | ---------------------------------------------------- |
| `--port`        | 8088    | Port, auf dem die Seite des Managers bereitgestellt wird |
| `--device-port` | 80      | Port, auf dem Geräte ihre API bereitstellen. Der Linux-Simulator verwendet 8780; deshalb gibt es diese Option. |

## Was der Manager erfasst und wo er es speichert

Alles, was der Manager dauerhaft speichert, liegt in `devmgr/data/devmgr.json` (wird durch .gitignore aus dem Repository ausgeschlossen, weil die Datei deine echten Adressen enthält):

* **Einstellungen** -- das zu durchsuchende Subnetz, das Aktualisierungsintervall (standardmäßig 10 s; im Einstellungsfenster 5/10/30/60 s), ob die regelmäßige Aktualisierung überhaupt läuft (standardmäßig aus; Scans und manuelle Aktualisierungen funktionieren trotzdem) und die Protokollierungsstufe.
* **Geräte** -- jedes jemals gefundene Gerät, identifiziert durch seine **MAC-Adresse**, die eine Eigenschaft, die DHCP nicht ändern kann. Ein Gerät, das auf eine Aktualisierung antwortet, ist online; seine Adresse, sein Hostname, seine Version, Signalstärke und Betriebszeit werden neu erfasst. Geräte, die nicht antworten, werden ausgegraut mit dem Zeitpunkt ihres letzten Kontakts angezeigt und bleiben in der Liste, bis du sie entfernst. Verschwunden zu sein darf nicht bedeuten, vergessen zu werden.

## Die Seite

* **Scan** -- prüft jede Adresse des konfigurierten Subnetzes jeweils mit einem `GET /api/status`, parallel, mit zwei Sekunden Wartezeit pro Antwort. Ein Scan läuft im Hintergrund; der Fortschrittsbalken wird live aktualisiert. Antwortende Geräte werden anhand ihrer MAC-Adresse mit der Liste zusammengeführt, sodass ein erneuter Scan vorhandene Einträge aktualisiert, statt Duplikate anzulegen.
* **Aktualisieren** -- der Zeitplaner liest den Status aller bekannten Geräte im konfigurierten Intervall erneut aus. Wichtig sind Zustandswechsel: "ist offline gegangen" und "ist wieder da" werden protokolliert, ein Gerät, das einfach weiterhin erreichbar ist, dagegen nicht.
* **Konfigurieren** (pro Gerät über die Zahnrad-Schaltfläche) -- liest `GET /api/config` des Geräts aus. Diese Antwort enthält für jede Einstellung Beschriftung, Typ, aktuellen Wert und Wertebereich oder Auswahlmöglichkeiten (bei einer `color`-Einstellung erscheint der Farbwähler des Browsers). Die Einstellungen sind in denselben Registerkarten gruppiert wie auf dem Touchscreen (Device, Time, Theme, Audio, openHAB, MQTT, Sensors). Es werden nur tatsächlich geänderte Felder gesendet; die Schaltfläche **Save** bleibt deaktiviert, bis etwas zu senden ist. Geheimnisfelder (das MQTT-Passwort) werden als `***` maskiert zurückgegeben und nur erneut gesendet, wenn du einen neuen Wert eingibst.
* **Mehrere Geräte bearbeiten** (Geräte auswählen, dann "Bulk edit") -- dasselbe Formular, mit einem Kontrollkästchen pro Feld, über das die Änderung für dieses Feld aktiviert wird. Die geänderten Felder werden parallel an alle ausgewählten Geräte gesendet, und das Ergebnis wird pro Gerät angegeben.
* **Firmware aktualisieren** -- für ein Gerät oder eine Auswahl. Das Image wird entweder automatisch pro Ziel ausgewählt (neueste `release/X.Y/oh-ez-touch-X.Y-
  <target>.bin`, andernfalls aus dem Build-Verzeichnis) oder als `.bin` hochgeladen und anschließend an jedes ausgewählte Gerät übertragen. Jedes Update wird geprüft, mit laufender Prozentanzeige übertragen und – entscheidend – nach dem Neustart **verifiziert**: Das Gerät muss auf `/api/status` mit der erwarteten Version und dem erwarteten Ziel antworten. Eine 200-Antwort allein für den Upload beweist nichts; auch die alte Firmware antwortet mit 200, wenn das Flashen fehlgeschlagen ist.
* **Gong** (Glockenschaltfläche) -- spielt den Türgong des Geräts **zwangsweise** ab, auch wenn der Summer stummgeschaltet ist, denn genau bei dem Panel, das du suchst, kennst du vermutlich dessen Einstellungen nicht. Die Stummschaltung selbst wird nicht geändert; der Gong bleibt danach wieder still.
* **Neustart** -- für ein Gerät oder eine Auswahl.
* **Kommentar und Löschen** -- mit dem Stift lässt sich eine Notiz wie "Flur, 2,8 Zoll" hinterlegen, die Neustarts übersteht; das Kreuz entfernt ein Gerät aus der Liste.
* **Konsole** -- das Feld unten zeigt, was der Manager im Hintergrund tut: Scans, Aktualisierungen, jede Anfrage an ein Gerät mit ihrem Ergebnis, Speichervorgänge und Updatephasen. Die Einträge lassen sich nach Stufe und Quelle filtern, anhalten und für Fehlerberichte kopieren. Solange die Konsole geschlossen ist, zählt ein Kennzeichen auf der Konsolenschaltfläche ungelesene Warnungen und Fehler.

Direktlinks: `http://localhost:8088/#config=<mac>` öffnet direkt den Einstellungsdialog eines Geräts.

## Die API des Managers

Die Seite ist ein Client einer kleinen JSON-API, die auch Skripte verwenden können:

| Route | Zweck |
| ----- | ------- |
| `GET /api/state` | Einstellungen, Geräte, Scanfortschritt, Updatefortschritt |
| `POST /api/settings` | Subnetz / Intervall / Aktualisierung / Protokollierungsstufe ändern (wird gespeichert) |
| `POST /api/scan` | Hintergrundscan starten: `{"subnet": "192.168.1.0/24"}` |
| `POST /api/refresh` | Alle Geräte jetzt aktualisieren |
| `GET /api/log?since=N&level=L` | Konsoleneinträge ab Cursor N |
| `POST /api/log/clear` | Konsole leeren |
| `GET /api/device/<mac>/config` | Einstellungsschema und -werte des Geräts (weitergeleitet) |
| `POST /api/device/<mac>/config` | Geänderte Felder an ein Gerät speichern |
| `POST /api/devices/config` | Sammelspeicherung: `{"macs": [...], "fields": {...}}`, Ergebnisse pro Gerät |
| `POST /api/device/<mac>/restart` | Gerät neu starten |
| `POST /api/device/<mac>/chime` | Türgong zwangsweise abspielen |
| `POST /api/device/<mac>/comment` | Notiz festlegen |
| `POST /api/device/<mac>/delete` | Aus der Liste entfernen |
| `GET /api/update/images` | Pro Ziel verfügbare Images |
| `POST /api/update/upload` | Eine `.bin` für den nächsten Lauf speichern (roher Inhalt, Header `X-Filename`) |
| `POST /api/update` | Updates starten: `{"macs": [...], "image": "auto"}` oder `{"image": "upload", "name": "x.bin"}` |

## Wissenswerte Regeln

* **Nicht angegebene Felder bleiben unberührt.** `POST /api/config` des Geräts wendet nur die übermittelten Felder an; wird ein Wert abgelehnt, wird die gesamte Anfrage zurückgerollt – für diese Anfrage wird auf dem Gerät nichts gespeichert. Das ist bewusst anders als beim Webformular des Geräts (`/save`), bei dem ein nicht angekreuztes Kontrollkästchen im Anfragetext einfach fehlt und als "aus" interpretiert würde.
* **Geheimnisse sind schreibgeschützt.** `GET /api/config` maskiert sie als `***`; wird die Maske zurückgesendet, ändert sich nichts. Ein echter Wert wird übernommen.
* **Erzwungene Signaltöne gelten nur einmal.** Das Erzwingungskennzeichen wird zusammen mit dem einzelnen in die Warteschlange gestellten Ton übertragen und verschwindet mit ihm; die Stummschaltung des Panels wird nie geändert.
* **Die Konsole protokolliert den Manager, nicht die Panels.** Was ein Gerät auf seiner eigenen seriellen Schnittstelle ausgibt, wird nicht über HTTP übertragen. Die Konsole zeigt jede Anfrage des Managers und die jeweilige Antwort – also genau den Teil, der sonst unsichtbar wäre.
