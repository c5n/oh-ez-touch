# mqttviz – Gesprächsnotiz

Datum: 2026-09-29. Kurze Zusammenfassung der bisherigen Arbeit an mqttviz, in vereinfachtem technischem Deutsch. Die vollständige Funktionsdokumentation steht in [mqttviz_de.md](mqttviz_de.md).

## Was mqttviz ist

Ein kleines Werkzeug, das den MQTT-Broker beobachtet und jedes OhEzTouch-Panel als Kästchen auf einer Fläche darstellt. Ein Python-Skript, eine Webseite, keine zusätzlichen Abhängigkeiten. Es liest, was die Panels veröffentlichen, und kann auf die Topics zurückveröffentlichen, auf die die Firmware hört.

## Zustand vor dieser Sitzung

- Ein Bild: die **Mesh-Ansicht**. Der Broker steht in der Mitte, die Panels bilden einen Ring darum, und jede Verbindung ist eine Feder.
- Beacons sind bewegliche Kästchen, die an jedes Panel gebunden sind, das sie empfängt. Die Länge der Verbindung richtet sich nach dem RSSI-Wert.
- Schieberegler für die Physik in drei Tabs: Panels, Beacons, Broker. Die Regler werden in der Einstellungsdatei gespeichert.
- Ansichtsoptionen für den Broker-Knoten und die Beacons.

## Anforderungen aus dieser Sitzung

Neues Physiksystem für die Anzeige einer Mesh-Netzwerktopologie:

- **Broker** – fester, zentraler Knoten.
- **WLAN-Access-Point** – neuer Objekttyp, anhand der BSSID identifiziert, fest positioniert. Verbindung zum Broker über LAN.
- **Panels** – feste BLE-Scanner. Jedes Panel ist mit einem Access Point verbunden (BSSID und Signalstärke aus den eigenen Topics).
- **BLE-Beacons** – beweglich.

Ziele:

1. Feste Objekte verteilen sich auf sinnvolle Weise. Ihre Position muss speicherbar sein.
2. Bewegliche Objekte ordnen sich anhand der Signalstärke und, falls vorhanden, des MQTT-Distanz-Topics an.
3. Physiksimulation für alle Knotenbewegungen.
4. Die Bewegung soll organisch und natürlich wirken. Animationen und Effekte sind erwünscht. Futuristisches Erscheinungsbild.

## Entscheidungen

- Die neue Topologieansicht kommt **zusätzlich** zur bestehenden Mesh-Ansicht hinzu. Über einen Schalter in der oberen Leiste wird die Ansicht ausgewählt. Für bestehende Benutzer ändert sich nichts; Standard bleibt `mesh`.
- **Anheften**: Ein feststehendes Objekt ziehen und loslassen – dadurch wird seine Position sofort angeheftet und gespeichert. Ein kleines Fadenkreuz auf dem Kästchen löst die Anheftung. Positionen werden als Anteile der Fläche (0..1) gespeichert und bleiben so bei einer anderen Fenstergröße erhalten. Beacons werden nie angeheftet.
- **Access Points**: eigener Kästchentyp, abgeleitet aus den Topics `system/bssid` und `system/ssid` der Panels. Eigener Tab für die Physikeinstellungen. Hat ein Panel keine BSSID, wird als Ersatz eine direkte Verbindung zum Broker verwendet.
- **Beacon-Platzierung**: Das Topic `ble/<addr>/distance` hat Vorrang; sein Wert wird anhand eines neuen Reglers (Pixel pro Meter, 10..200, Standardwert 60) skaliert. Die RSSI-Zuordnung bleibt der Ersatz. Bei zwei oder mehr Scannern bestimmt das Federnetzwerk die Beacon-Position durch Triangulation.
- **Effekte**: standardmäßig aktiviert, über einen *FX*-Schalter in der oberen Leiste deaktivierbar. Die Physik bleibt in beiden Fällen gleich.

## Implementierte Änderungen

Server (`mqttviz/mqttviz.py`):

- Beacon-Registrierung speichert die Distanz für jeden Scanner.
- Neue Einstellungen: `view_mode`, `show_fx`, `positions`, `phys_aps` und der Regler `metre_px`.
- Neue Routen: `POST /api/position` und `POST /api/position/delete`.

Client (`mqttviz/web/`):

- Neue Objekttypen: Access-Point-Kästchen und ein echtes Broker-Kästchen.
- Topologiephysik: LAN-Feder vom Broker zum AP, WLAN-Feder vom AP zum Panel (Ruhelänge abhängig vom WLAN-Signal), Distanzleine für Beacons, angeheftete Kästchen als feste Hindernisse bei der Überlappungsberechnung.
- Der Ansichtswechsel ist ein Übergang: Die Federn bewegen die Kästchen in das andere Layout.
- Effektebene: Punktgitter und Vignette als Hintergrund, Radarschwenk, pulsierender Broker-Kern mit rotierendem Ring, atmende Lichtkränze, Ein- und Ausblendanimationen, wachsende Verbindungen mit Strichfluss, Beacon-Spuren, Partikel mit additiven Schweifen, Aufblitze bei Ankunft und Anheben bei Mauszeigerkontakt.
- Detailansicht für APs, Anheft-Schaltfläche in der Detailansicht, neue Texte auf Englisch und Deutsch.

## Überprüfung

- Syntaxprüfungen erfolgreich (`py_compile`, `node --check`).
- API-Tests mit curl: anheften, lösen, Begrenzung, Speichern über einen Neustart hinweg, Ansichtsmodus, FX-Schalter und `phys_aps`-Regler.
- Layoutprüfung in einem Headless-Browser: Mesh-Ring bleibt erhalten, die Topologie findet ohne Überlappungen zur Ruhe, angeheftetes Panel bleibt exakt an der gespeicherten Position, Beacon nähert sich der Distanzleine an (etwa 90 px für 1.5 m bei 60 px/m statt der 248 px der RSSI-Ersatzberechnung).

## Offene Punkte

- Die Screenshots in der Dokumentation wurden noch nicht aktualisiert (siehe Aufgabenliste).
- Für Server und Webseite gibt es kein Unit-Test-Framework; die Prüfungen erfolgten manuell. Der SweetHome3D-Konverter hat eines (`mqttviz/test_sh3d.py`).

## Nachtrag: Layouts (am selben Tag)

Anforderung: mehrere Layouts speichern und wiederherstellen, eine Sicherungsdatei anlegen und sicherstellen, dass die aktuelle Anordnung niemals verloren geht.

Entscheidungen:

- Ein Layout ist eine vollständige Anordnung: die angehefteten Positionen **und** die Ansicht, zu der sie gehören (Mesh oder Topologie). Beim Wiederherstellen wechselt die Fläche zu dieser Ansicht, so wie beim Umschalten der Ansichten.
- Speicherung unter Namen im Dialog *Layouts* (neue Schaltfläche in der oberen Leiste). Wird derselbe Name zweimal verwendet, gilt die neuere Anordnung.
- Der Server speichert neben dem Zustand eine Sicherungsdatei, `mqttviz/data/layouts.json`, und schreibt sie bei jeder Änderung neu: die aktuelle Anordnung, alle gespeicherten Layouts und den Zustand unmittelbar vor der letzten Wiederherstellung. Eine fehlgeschlagene Wiederherstellung kostet nichts.
- Der Dialog schreibt denselben Inhalt in eine eigene Datei des Benutzers (Download) und liest solche Dateien wieder ein (Import). Beim Import werden Einträge zusammengeführt: Namen aus der Datei überschreiben gleichnamige lokale Namen, alle anderen Einträge bleiben unberührt.
- Bevor irgendetwas davon begann, wurde die aktuelle Zustandsdatei von Hand nach `mqttviz/data/backups/` kopiert – die Anordnung von vor Einführung der Funktion liegt damit zweimal auf der Festplatte.

Neue API: `POST /api/layout/save|load|delete|import`.

Von Hand überprüft: anheften, speichern, erneut anheften, wiederherstellen (Positionen exakt zurückgesetzt, `before_load` in der Sicherungsdatei), löschen, importieren (zusammengeführt, fehlerhafte Einträge verworfen, kein Abbruch), Zustand bleibt über Neustart hinweg erhalten, tatsächliches Datenverzeichnis unverändert (Hashes gleich). Die laufende Instanz auf Port 8089 verwendet bis zu ihrem nächsten Neustart weiterhin den alten Code – für sie ändert sich nichts, und der neue Einstellungsschlüssel fehlt bis dahin.

## Nachtrag: Zoom (am selben Tag)

Anforderung: Zoom für die Topologieansicht; der Arbeitsbereich soll die vierfache Fläche bieten.

Entscheidungen:

- **Die Bühne ist der Arbeitsbereich**: in jede Richtung doppelt so groß wie die Fläche, insgesamt viermal so viel Fläche, mit dem klassischen Flächenbild in der Mitte. Physik und Zeichnung verwenden Arbeitsbereich-Pixel; an der Physik musste nichts geändert werden.
- Eine **Kamera** (Zoom 0.5 bis 2.5, Verschieben) zeigt den Arbeitsbereich durch den Ansichtsbereich. Die Startansicht hat Zoom 1 und ist zentriert – beim Wechsel zur Topologie ändert sich nichts Sichtbares. Bei 0.5 ist der gesamte Arbeitsbereich auf einmal zu sehen.
- Eingabe: Mausrad zoomt (am Mauszeiger verankert, Trackpad-Zoomgeste funktioniert), Ziehen auf leerer Fläche verschiebt die Ansicht, drei Schaltflächen in der Ecke der Fläche (hineinzoomen, herauszoomen, sanft zur Startansicht). Die Mesh-Ansicht behält immer die Startansicht – sie ist ein eigenes, festes Bild.
- Die Kamera bewegt sich wie die Kästchen gleichmäßig auf ihr Ziel zu (exponentiell) und wird als Anteile (`cam`) mit den Einstellungen gespeichert, nach dem Speichern übernommen und pro Sitzung einmal angewendet.
- **Migration**: Positionen waren früher Anteile der Fläche. Beim Laden wird eine ältere Datei einmalig in Anteile des Arbeitsbereichs umgerechnet (`new = 0.25 + 0.5 * old`, auf beiden Achsen, für Positionen und alle Positionen der Layouts). Die Migration wird über eine `pos_space`-Markierung in den Einstellungen abgesichert. Jeder Pin landet genau dort, wo das Auge ihn zurückgelassen hat – geprüft: Ein alter Pin bei den Flächenanteilen 0.3/0.3 bleibt in der Startansicht auf demselben Bildschirmpixel.

Überprüft: Migrationsformel und Idempotenz über Neustarts hinweg, Begrenzung der Kamera (`zoom 9 -> 2.5`, ungültige Daten -> Startansicht), Geometrie im Headless-Browser (Broker an der alten Bildschirmposition, Arbeitsbereich genau 2x so groß, Kamera aus den Einstellungen übernommen, Mesh erzwingt Startansicht, Zoom-Schaltflächen nur in der Topologie). Es gilt derselbe Hinweis zur laufenden Instanz: Die Migration wird einmalig bei ihrem nächsten Neustart auf ihre Daten angewandt, ohne dass das Auge einen Unterschied bemerkt.

## Nachtrag: die Raumansicht (2026-10-02)

Anforderung: die Topologieansicht in 3D, feste Objekte im Raum platzierbar und die Wände eines SweetHome3D-Hauses zur Platzierung sichtbar – halbtransparent wie eine futuristische Laserprojektion.

Entscheidungen:

- Ein **dritter Ansichtsmodus**, `space`, neben Mesh und Topologie. Die Topologie behält ihre 2D-Pins; die Raumansicht erhält eigene Positionen in Metern (`positions3d`, dieselben Schlüssel), und Layouts speichern beide Arten.
- **three.js, mitgeliefert** als einzelnes minimiertes ES-Modul-Bundle (0.186.1 mit OrbitControls, CSS2DRenderer und Bloom-Verarbeitungskette), erstellt mit esbuild über `web/vendor/build.sh`. Eine Datei, die über eine feste Whitelist bereitgestellt wird, keine Import-Map, keine Downloads aus dem Internet: Die Seite funktioniert in einem LAN, und auf dem Rechner, auf dem mqttviz läuft, wird weiterhin nur Python 3 benötigt.
- **Das Haus wird beim Upload serverseitig konvertiert**, mit der Standardbibliothek: Eine `.sh3d`-Datei ist ein ZIP-Archiv, dessen `Home.xml` (SweetHome3D 5.3+) Wände, Räume, Ebenen sowie Türen und Fenster in Zentimetern enthält. `mqttviz/sh3d.py` liest nur diese Elemente, rechnet sie in Meter um (Grundriss-x -> x, Grundriss-y -> z, y nach oben), zerlegt runde Wände (`arcExtent`) in Abschnitte von 10 Grad, die nur an den Bogenenden verschlossen werden, und begrenzt die Mengen, damit eine ungewöhnliche Datei eine Ablehnung und keinen Absturz des Servers verursacht. Der serialisierte Java-`Home`-Eintrag älterer Dateien wird nicht gelesen; solche Dateien werden mit einem Hinweis zur Lösung abgelehnt.
- **Erscheinungsbild**: Wände als zusammengeführte Prismengeometrie pro Ebene mit einem kleinen Shader – additiv, ohne Tiefenpuffer-Schreiben, Fresnel-Rand, Scanlinien und ein aufsteigendes Scanband nur auf den senkrechten Flächen, leichtes Flackern – dazu helle Kantenlinien, blasse Raumflächen mit Umrissen und Namen, orange Rahmen für Türen und Fenster sowie ein Projektorring und ein Gitter unter dem Haus. Bei aktiviertem FX wird Bloom verwendet. Der dunkle Hintergrund der Seite ist der Hintergrund der *Szene*, nicht die klare Rendererfarbe: Die Renderziele des Compositors übernehmen eine nicht konvertierte klare Farbe, die grau erschien.
- **Platzierung erfolgt anhand des Grundrisses, nicht per Raycasting auf die Wände.** Die erste Version verwendete Raycasting auf Wandnetze; da sich die Kamera außerhalb des Hauses befand, landete jeder Ziehvorgang an der nächstgelegenen Außenwand – die Wände sahen aus wie Licht, hielten den Zeiger aber wie Materie auf. Jetzt bewegt sich der Zeiger über die unsichtbare Bodenebene der angezeigten Etage. Ist er weniger als 35 cm von einer Wandfläche entfernt, wird das Objekt auf der Zeigerseite an diese Wand gesetzt, und zwar auf der vom Zeiger getragenen Höhe (Standardhöhe je Objekttyp, mit Shift änderbar). Das Dock weist jedem Objekt einen festen Platz zu, sodass das Herausnehmen eines Objekts die anderen nicht unter der Hand verschiebt.
- **Beacons in Metern**: einige Schritte des Gradientenabstiegs anhand der Distanzabweichungen zu den platzierten Panels, die sie empfangen, mit Warmstart aus der letzten Lösung; bei nur einem Empfänger eine Kreisbahn im entsprechenden Abstand. `line_timeout` der Beacon-Physik bestimmt weiterhin, welche Empfangsmessungen berücksichtigt werden.
- `app.js` bleibt ein klassisches Skript und die Seite; es übergibt dem Modul die benötigten Daten über `window.mqttvizHost` und ruft nach jedem Abruf und Ansichtswechsel `MqttvizSpace.update()` auf. Die 2D-Zeichenschleife pausiert, solange die Raumansicht aktiv ist. Die Kästchen folgen weiterhin den tatsächlichen Daten (auch die Kästchen der Access Points bleiben in der Raumansicht erhalten, weil die Detailansicht sie ausliest).

Überprüft: `test_sh3d.py` (Ebenen, Einheiten, Achsen, Neigungen, runde Wände und ihre Abschlüsse, Ablehnungen); Routen mit curl (Import, ungültige Daten abgelehnt, `/vendor/../` wird nicht bereitgestellt, NaN-/inf-Positionen abgelehnt, ein Layout speichert, verliert und stellt einen 3D-Ort wieder her, `before_load` in der Sicherungsdatei); Headless-Chromium über CDP mit `#debug` gesteuert (dadurch schreibt die Seite auch Objekte und Bildschirmpositionen der Raumansicht): Jedes vom Dock gezogene Panel landete an der vorgesehenen Wand in 1.4 m Höhe oder auf dem vorgesehenen Boden, Positionen blieben nach dem Neuladen erhalten, eine Berührung öffnete die Detailansicht mit *aus dem Haus nehmen*, Objekte und Verbindungen einer ausgeblendeten Ebene verschwanden, Beacons wurden zwischen den Panels trianguliert; Mesh und Topologie werden weiterhin wie zuvor gezeichnet.

## Nachtrag: die Bilder (am selben Tag)

Anforderung: die Dokumentation aktualisieren. Der offene Punkt waren die Bilder des Leitfadens – er hatte keine.

- `mqttviz/demo/` macht sie reproduzierbar, so wie `doc/openHAB/make-screenshots.sh` für das Panel: `demo.py` startet das echte mqttviz mit `--no-mqtt` und speist eine erfundene Flotte (vier Panels, zwei Access Points, zwei Beacons) über `note_message` ein – denselben Einstiegspunkt, den der Broker-Thread verwendet. `demo_house.py` schreibt eine zweistöckige `Home.xml`; `screenshots.mjs` steuert Headless-Chromium über das DevTools-Protokoll mit dem integrierten Fetch und WebSocket von Node 22, ohne npm-Pakete, und richtet die Seite vor jedem Bild über die API ein (Pins, Layout, Hausimport, Positionen).
- Die Bilder machten drei Fehler sichtbar, die daraufhin behoben wurden: Die UI-Merkmale der Panel-Kästchen waren mit „undefined“ beschriftet – ein lokales `t` für die Topic-Suche in `drawBox` verdeckte den Übersetzer. Außerdem führten die Beacon-Linien noch zu einem Panel auf einer ausgeblendeten Ebene, und ohne geladenes Haus setzte die Kamera den Rahmen um die Hausgrenzen, nicht aber um das davorliegende Dock. Die Layoutliste zeigt jetzt, wie viele Positionen ein Layout im Haus speichert, und der Wandregler ist ausgeblendet, solange kein Haus geladen ist.

## Nachtrag: Längen, Beacons mit Vertrauen in nahe Messwerte und der Ansichtswürfel

Anforderungen aus einigen Tagen Erprobung am echten Haus: sehr alte Einträge aus den Topic-Listen der Panels entfernen, Längen in Metern an den 3D-Linien anzeigen, den Beacon korrigieren, der außerhalb der Werkstatt lag, in der er sich befand, einen Ansichtswürfel für die Kamera und eine Größensteuerung für die gesamte Oberfläche hinzufügen.

- **Die Ansicht zeigt aktuelle Daten.** `/api/state` lässt jetzt Topics aus, von denen seit einer Stunde nichts mehr empfangen wurde, sowie Panels, die seit einem Tag stumm sind. Die Registrierung behält sämtliche Daten – gefiltert wird die Antwort, nicht der Speicher –, daher erscheinen beide sofort wieder, sobald sie erneut senden.
- **Linienlängen.** Jede Verbindung – Panel zu Access Point, Access Point zu Broker, Beacon zu Panel – zeigt in der Raumansicht in der Mitte ihre Länge in Metern. Die Beschriftung ist ein CSS2D-Span in einem positionierten Div: Der Renderer verwaltet die Transformation des Divs; der Span erhält die Drehung, die auf dem Bildschirm entlang der Linie gemessen wird, und wird bei mehr als 90° aufrecht gedreht.
- **Beacons, die nahen Messwerten vertrauen.** Das echte Haus zeigte, was die sauberen Messwerte der Demo nicht zeigen konnten: Panels auf anderen Stockwerken meldeten für Panels in fünf Metern Entfernung eine geschätzte Distanz von 25 bis 35 m. Eine ungewichtete Anpassung nach kleinsten Quadraten platzierte einen Beacon der Werkstatt damit dreißig Meter außerhalb des Hauses. Außerdem konvergierte der Gradientenabstieg nie – nach vierzig festen Schritten endete er auf halbem Weg. Jetzt wird jede Messung mit 1/d² gewichtet: Ein starkes „2 m“ ist eine Behauptung, ein schwaches „28 m durch zwei Stockwerke“ eine Vermutung. Der beste Punkt wird zuerst durch eine grobe Suche über den Hausgrundriss bestimmt, denn die Kreise zweier Messungen schneiden sich zweimal, und allein der Gradientenabstieg findet den Schnittpunkt, in dessen Nähe er startet. Die beste Zelle dieser Suche wird anschließend mit dem bisherigen Gradientenabstieg verfeinert. Dieser läuft nur bei geänderten Empfangsmessungen und höchstens alle halbe Sekunde.
- **Eine gekürzte Ebenen-ID.** Der nach oben versetzte Beacon blieb im Keller: Der Server speicherte die Ebenen-ID jedes Ortes auf vierzig Zeichen gekürzt (`[:40]`), während die IDs des Hauses zweiundvierzig Zeichen lang sind (`level-` + eine UUID). `levelById` fiel dann für jedes platzierte Objekt auf `houseLevels[0]`, den Keller, zurück. Die Grenze beträgt nun vierundsechzig Zeichen, und die Seite löst alte, gekürzte IDs über ihr eindeutiges Präfix auf, sodass bereits gespeicherte Orte weiterhin auf ihre Ebenen verweisen.
- **Der Ansichtswürfel.** Ein abgeschrägter Würfel unten links in der Raumansicht: sechs beschriftete Flächen für Ansichten direkt entlang einer Achse, zwölf Kantenflächen für diagonale Ansichten im 45°-Winkel, acht Eckflächen für isometrische Ansichten. Er wird nach dem Hauptdurchlauf in ein eigenes, ausgeschnittenes Quadrat mit gelöschtem Tiefenpuffer gezeichnet und schwebt deshalb über dem Haus, ohne Bloom zu erhalten. Er dreht sich mit der Kamera (inverse Quaternion der Kamera); eine Berührung fliegt entlang des kürzesten Bogens dorthin, interpoliert mit Slerp um das Umlaufziel und behält dabei den Abstand bei. Beim Ziehen wird an die Umlaufsteuerung weitergeleitet, sodass der Würfel auch als deren Griff dient. Das Ziehen wird von einer Berührung anhand derselben fünf Pixel unterschieden, die auch sonst einen Ziehvorgang ausmachen. An den Polen bleibt ein Hauch des Azimuts erhalten, aus dem man kam, und `maxPolarAngle` musste seine Grenze von 0.495π aufgeben: Die Unterseite zeigt von unten, und die Begrenzung hätte sie sofort wieder zurückschnappen lassen.
- **GUI-Größe.** Eine Einstellung von 70 bis 160 Prozent: `--gui-scale` am Wurzelelement, `zoom` für die Oberfläche (obere Leiste, Bedienfelder, Dialoge und Symbolleiste der Raumansicht), `calc()` für die Schaltflächen des Würfels und die Beschriftungen der Raumansicht – deren Positionen dürfen nicht gezoomt werden, da sie aus der Projektion stammen – sowie für den in der Fläche gezeichneten Würfel selbst, dessen Größe in Seitenpixeln angepasst wird. Der Stapel in der Ecke (Hilfetext, Schaltflächen, Würfel) wird gemessen statt geschätzt und passt daher bei jeder Skalierung.
- Die Kästchen der Mesh-Ansicht werden jetzt über Indexzuordnungen statt mit `boxes.find` in der gesamten Liste gefunden; außerdem wurden kleine Korrekturen an Funken, Scans und Wellen vorgenommen.

Überprüft: Geometrie und Auswahl mit dem mitgelieferten three.js (26 Netze, jede Fläche, Kante und Ecke wählt die richtige Richtung, der Slerp-Mittelpunkt und der 180°-Fall exakt); Beacon-Position anhand des aktuellen `/api/state` des echten Hauses – zuvor außerhalb bei (-5.3, -6.9), danach in der Werkstatt bei (1.8, 0.8), mit Stockwerk entsprechend dem stärksten Empfang, sobald die gekürzten Ebenen-IDs aufgelöst waren.
