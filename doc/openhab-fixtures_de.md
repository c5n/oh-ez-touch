# Tests mit einem echten openHAB

`main/sim/sitemap_fixture.cpp` reicht aus, um einen Bildschirm zu zeichnen, und die Host-Tests in `test/host` reichen aus, um festzuhalten, wie der Parser damit umgeht. Beides kann nicht sagen, was openHAB *tatsächlich sendet*. Das ist jedoch die interessante Frage: Die Fixtures und ihre Tests wurden ursprünglich anhand der REST-Dokumentation geschrieben, und vier ihrer Annahmen waren falsch.

Deshalb enthält `test/openhab/` Sitemaps und Elemente, auf die ein echter Server gerichtet werden kann. Sie sind keine Demo des Panels, sondern so gestaltet, dass sie Grenzfälle erreichen: einen tieferen Navigationsbaum, als ein Screenshot zeigt, je ein Element jedes von openHAB unterstützten Typs und Seiten, die eigens dafür zusammengestellt wurden, die festen Arrays des Panels zu überschreiten.

Alles Folgende wurde am **15.09.2026** mit openHAB 5.2.1 und am 02.10.2026 mit dem Sitemap-Stream gemessen. Wenn hier steht, dass openHAB etwas tut, dann hat dieser Server es getan – nicht unbedingt die Dokumentation.

## Installation

openHAB liest `.items`- und `.sitemap`-Dateien aus seinem Konfigurationsverzeichnis und lädt Änderungen innerhalb von etwa zehn Sekunden neu. Bei einer Snap-Installation:

```bash
OH=/var/snap/openhab/current/conf      # "current" is a symlink to the revision
cp test/openhab/items/*.items       $OH/items/
cp test/openhab/sitemaps/*.sitemap  $OH/sitemaps/
```

Dieses Verzeichnis gehört **standardmäßig root**, und das Erstellen von Elementen über REST ergibt eine 401-Antwort. Auf einem frischen Rechner werden daher entweder `sudo` oder ein einmaliges `chmod` der beiden Verzeichnisse benötigt. Achte in `openhab.log` auf `Loading DSL model '<name>'`, um zu erkennen, dass die Datei geladen wurde, und auf die Validierungsfehler, die bei einem Fehlschlag ausgegeben werden:

```bash
tail -f /var/snap/openhab/current/userdata/logs/openhab.log
```

Eine weitere Bestätigung liefert `/rest/sitemaps`: Eine Sitemap, die nicht geparst werden konnte, steht nicht in der Liste.

Anschließend müssen die Elemente Statuswerte erhalten. Ein Element, dem noch nie etwas mitgeteilt wurde, hat den Status `NULL`; ein Panel voller `NULL`-Werte sagt nichts darüber aus, wie das Panel zeichnet:

```bash
test/openhab/seed-states.sh                    # or ... http://host:8080
```

## Panel auf eine Sitemap zeigen lassen

```bash
tools/ohez_ctl.py set oh_sitemap oheznav       # applies without a restart
tools/ohez_ctl.py wait-page
```

`set` schreibt eine echte `config.json`. Isoliere einen Lauf daher, indem du `OHEZ_CONFIG_DIR` auf ein entbehrliches Verzeichnis zeigen lässt.

## Inhalt

| Sitemap | Datei | Zweck |
| --- | --- | --- |
| `demo` | `demo.sitemap` | Sechs Widget-*Typen* auf einer Seite. Der schnellste Weg, Themes zu vergleichen oder eine Steuerung von Anfang bis Ende zu prüfen. |
| `oheztest` | `oheztest.sitemap` | Formen, die in `demo` nicht vorkommen: eine Group mit Aggregatfunktion, eine dimensionierte `Number:Temperature`, eine `Number:Dimensionless`, ein Dimmer und ein String mit Leerzeichen. |
| `oheznav` | `oheznav.sitemap` | Ein fünfstufiger Navigationsbaum, ein Element jedes openHAB-Typs und vier Seiten, die absichtlich ein Panel-Limit überschreiten. |
| `ohezevents` | `ohezevents.sitemap` | Alles an einem Widget außer seinem Status: Farbregeln, eine Sichtbarkeitsregel auf einem Widget und einem Frame, ein schreibgeschütztes Element, Statusoptionen, Befehlsoptionen, eine MAP und ein statisches Icon. Über `/rest/sitemaps/events` beobachtet. |

`demo.*` war bereits auf dem Testserver vorhanden. Die anderen drei Dateien wurden geschrieben, um Fragen zu beantworten, die mit den Fixtures offenblieben.

### Insbesondere `ohezevents`

`EV_Show` steuert beide Sichtbarkeitsregeln. `EV_Temp` steuert die drei Farbregeln: Unter 5 °C ist der Wert hellblau; über 25 °C ist er orange und das Icon rot; über 30 °C ist auch die Beschriftung rot. Ändere die Werte über REST und beobachte, wie sich die Kacheln ohne Neuladen der Seite aktualisieren:

```bash
curl -X PUT -H 'Content-Type: text/plain' --data-binary '30 °C' \
     http://localhost:8080/rest/items/EV_Temp/state
curl -X PUT -H 'Content-Type: text/plain' --data-binary ON \
     http://localhost:8080/rest/items/EV_Show/state
```

Die MAP auf `EV_Window` verwendet die mitgelieferte `de.map`. Ohne installiertes MAP-Transformation-Add-on zeigt openHAB den unveränderten Status an.

### Insbesondere `oheznav`

```
OhEzTouch Navigation
├── Ground Floor → Living Room → Lighting → Scenes     ← four levels below home
│   ├── Kitchen
│   └── Lights On              (Group → openHAB builds the member page)
├── First Floor → Bedroom
│   ├── Average [20,2 °C]      (Group:Number:AVG)
│   └── Total [1335 W]         (Group:Number:SUM)
├── Sensors                    (Contact, DateTime, Location, String)
└── Edge Cases → Too Many Tiles / Framed
```

Die gezielt angesteuerten Grenzfälle, bei denen jeweils etwas entdeckt wurde:

- **Scenes** liegt vier Ebenen unterhalb der Hauptseite. Eine größere Tiefe war bei der Navigation des Panels zuvor nie getestet worden.
- **Too Many Tiles** bietet zehn Widgets an, während `ITEM_COUNT_MAX` sechs beträgt.
- **Edge Cases** enthält eine Selection mit zwölf Einträgen gegenüber `ITEM_SELECTION_COUNT_MAX` von zehn, eine Beschriftung mit vierzig Zeichen gegenüber `STR_LABEL_LEN` von zweiunddreißig sowie `Außentemperatur Süd` für den Beschriftungskürzer. Dieser übergab früher Bytes über 0x7F an `isspace()`.
- **Framed** besteht aus zwei `Frame`-Blöcken. Siehe weiter unten – das war überhaupt kein Grenzfall.
- `NAV_Unset` erhält nie einen Befehl und bleibt deshalb `NULL`.
- `NAV_Spaced` enthält `Hello World`. Diese Form bringt die Icon-URL durcheinander.

## Was ein echtes openHAB sendet

Die Formen, die falsch eingeschätzt wurden und nun in den Fixtures enthalten sind und von `test/host/main/test_sitemap_parse.cpp` festgehalten werden:

- **Jedes Widget enthält `mappings`** – ein leeres Array, wenn in der Sitemap keine Zuordnungen definiert sind. Ein leeres JSON-Array ist für ArduinoJson *truthy*. Daher war `if (widget["mappings"])` für jedes von einem echten Server gesendete Widget wahr und der Fallback des Parsers auf `item.commandDescription.commandOptions` wurde mit openHAB 5 nie ausgeführt. Der Parser prüft jetzt die Größe.
- **`Text label="..." { ... }` enthält überhaupt keinen Schlüssel `item`** – es ist ein Text-Widget mit einem `linkedPage` und sonst nichts. Das ist die gängigste Methode, eine Unterseite zu erstellen, und entspricht dem `type_link` des Panels.
- **Ein `Group`-Widget enthält beides**: sein Gruppenelement mit `members`, `groupType` und `function` *sowie* ein `linkedPage` zu einer von openHAB erzeugten Mitgliederseite. Der Status der Gruppe ist echt und aggregiert; die Kachel wird also wie jede andere aktualisiert. Änderungen kommen als `GroupItemStateChangedEvent` über den Ereignisstream auf `items/<group>/<member>/statechanged` an. Deshalb abonniert das Panel diesen Themenpfad auch für Gruppen.
- **Ein Player enthält von openHAB selbst geschriebene Mappings** – PREVIOUS, PAUSE, PLAY, NEXT – und zwar aus einer einfachen Zeile `Default item=...`. Der Typ des Elements muss Vorrang vor dem Vorhandensein von Mappings haben, sonst wird die Kachel zu einer Selection.
- **Bei einem dimensionierten Element kommt die Einheit dreimal vor**: im Status (`"21.5 °C"`), im Widget als `unit` und im Element als `unitSymbol`. Der Connector liest keines dieser Felder; Temperaturen werden nur deshalb richtig angezeigt, weil das Anzeigemuster die Einheit wiederholt.
- **Das `pattern` des Widgets wird aus `stateDescription` des Elements abgeleitet**, nicht umgekehrt. Bestätigt mit einer `.items`-Beschriftung, die das Muster enthielt, und einer `.sitemap`-Zeile ohne Muster.
- **Eine einfache `Number` wird als `"10.000000"` gelesen**: sechs Nachkommastellen, keine Einheit, unabhängig vom Anzeigemuster.
- **Ein nicht initialisiertes Element wird als vier Zeichen `"NULL"` gelesen** – weder als JSON-null noch als leere Zeichenkette –, während die von openHAB erzeugte Beschriftung `-` lautet.
- **IDs von Unterseiten sind Widget-IDs** (`1_5`); auf einer von einer Gruppe erzeugten Seite ist die `widgetId` jeder Zeile der *Elementname*.
- **Eine fehlende Sitemap ergibt 404 und einen doppelt JSON-kodierten Body**, dessen oberste Ebene also eine Zeichenkette ist. `/state` eines fehlenden Elements ergibt 404 mit einem einfachen Fehlerobjekt. Beide Fälle werden anhand des Statuscodes abgelehnt, bevor der Parser sie sieht.
- **Icon-Sätze enthalten ausschließlich SVG.** `/icon/<name>?state=..&format=png` ergibt auf einem Standardserver 404; `format=svg` ergibt 200.
- Ein Server mit deutschem Gebietsschema schreibt den Wert mit einem **Dezimalkomma** in die Beschriftung (`Temperature [21,4 °C]`). Der gesamte Inhalt einschließlich Komma wird entfernt.
- **openHAB macht sich über mDNS bekannt**, und zwar als `_openhab-server._tcp.local` am REST-Port sowie `_openhab-server-ssl._tcp.local` auf 8443, bei beiden mit dem TXT-Record `uri=/rest`. Eine Unicast-Response-Abfrage – das `QU`-Bit, mit dem ein Client einen gewöhnlichen flüchtigen Socket verwenden kann, statt der Gruppe beizutreten – wird im kabelgebundenen Netzwerk innerhalb von 56 bis 70 ms in einem Paket mit PTR, A, TXT und SRV beantwortet. Der Owner-Name des SRV-Records ist ein Kompressionszeiger mitten in den Daten des PTR-Records, der selbst mit einem Zeiger auf die Frage endet. Ein Parser, der den Zeigern nicht folgt, findet daher nichts. Dieses Paket befindet sich Byte für Byte in `test/host/main/test_mdns_query.cpp`; es wurde am 18.09.2026 aufgezeichnet und dient als Grundlage für die Serverliste des Einstellungsbildschirms.
- **`transformedState` gibt es nicht mehr.** Die von openHAB formatierte Darstellung – eine `MAP(...)`-Transformation, ein Datumsformat oder ein Dezimalkomma – steht nur in `[...]` der Beschriftung. Der Parser verwendet diesen Wert als transformierten Status.
- **Ein von einer Sichtbarkeitsregel ausgeblendetes Widget ist gar nicht auf der Seite enthalten.** Es fehlt in `widgets`; es wird nicht mit `"visibility": false` gesendet. Seine Ereignisse kommen weiterhin auf dem Sitemap-Stream an und enthalten im ausgeblendeten Zustand `"visibility": false`.
- **Die Kinder eines ausgeblendeten Frames melden sich selbst als sichtbar.** Das Ereignis des Frames selbst meldet ihn als verborgen; das des Kindes meldet `"visibility": true`, unabhängig davon, was mit dem Frame ist.
- **Befehlsoptionen spiegeln Statusoptionen wider.** Bei einem Element mit ausschließlich `stateDescription`-Optionen enthält die Liste dieselben Optionen wie `commandDescription.commandOptions`.
- **Der Sitemap-Stream** (`/rest/sitemaps/events`):
  - `POST .../subscribe` antwortet mit 200 und der ID am Ende von `context.headers.Location`, einschließlich des im Request angegebenen Hosts.
  - Ein GET für eine openHAB unbekannte ID wird nie beantwortet: weder mit Statuszeile noch mit Fehler.
  - Auch ein GET für eine gültige ID wird erst beantwortet, wenn das erste Ereignis eintrifft. Selbst die Statuszeile wartet; auf einer ruhigen Seite ist sie der Keepalive. In einer Messung dauerte das 50 s.
  - Der Keepalive lautet `{"TYPE":"ALIVE",...}` und kommt ungefähr einmal pro Minute. Bei einer bearbeiteten Sitemap-Datei wird `{"TYPE":"SITEMAP_CHANGED",...}` gesendet.
  - Ein Widget-Ereignis ist 500 bis 900 Byte lang und enthält:
    - `widgetId`
    - die formatierte Beschriftung
    - `labelcolor`, `valuecolor` und `iconcolor`, wenn eine Regel greift
    - `visibility`, `reloadIcon` und `descriptionChanged`
    - das ganze Element einschließlich seines rohen Status
    - `sitemapName` und `pageId`
  - Auf gewöhnlichen Seiten lauten Widget-IDs `1_0`, `1_5`, `1_50`, … Auf einer von einer Gruppe erzeugten Seite sind sie die Elementnamen der Mitglieder. Die ID der Hauptseite ist der Sitemap-Name.
- **`GET /rest/sitemaps` liefert ein flaches Array**, ein Objekt pro Sitemap mit `link`, `name`, `label` und einem `homepage`-Objekt. Dessen `widgets`-Array ist hier **leer**, unabhängig davon, wie viele Widgets die Seite tatsächlich hat; Widgets kommen erst mit der Seitenanfrage. Pro Sitemap sind es ungefähr 210 Byte. `label` ist optional: Eine Zeile `sitemap x label="..."` setzt es; ohne eine solche Zeile fehlt der Schlüssel `label` vollständig. Dieses Format wird im Einstellungsbildschirm und im Webformular als Sitemap-Auswahl angeboten und mit einem Filter geparst, der `name` und `label` behält und den Rest verwirft – `test/host/main/test_sitemap_list.cpp`, anhand einer Aufzeichnung vom 18.09.2026.

- **Der Status eines Image-Elements ist das Bild**, gemessen am 02.10.2026 mit openHAB 5.2.1 und einer über die UI verwalteten Sitemap:
  - Auf einer Sitemap-Seite enthält das Widget das Element mit dem vollständigen Status `data:image/jpeg;base64,...` sowie nochmals den vorigen Status in `lastState`. Ein 14-KB-JPEG erzeugte eine 23,8-KB-Seite mit nur einem Widget. Das Panel kürzt beim Einlesen jeden JSON-String mit über 512 Byte (`main/openhab/json_squeeze.c`), ebenso auf den Ereignisstreams, wo ein neuer Snapshot ein Widget-Ereignis derselben Größe ist.
  - `url` des Widgets lautet unabhängig davon, ob ein Element dahinterliegt, `/proxy?sitemap=...&widgetId=...`. Für ein Widget mit Element antwortet openHAB mit **HTTP 500** („URL 'null' is not a valid URL“). Das Panel verwendet diese URL nie.
  - `GET /rest/items/<name>/state` antwortet standardmäßig mit `text/plain` und der Daten-URI sowie mit dem **rohen JPEG** und `Content-Type: image/jpeg`, wenn `Accept: image/*` oder `image/jpeg` gesetzt ist. Das fordert das Panel an.
  - Ein Image-Widget mit `url=` ohne Element enthält überhaupt kein `item`. Das Panel lässt es aus.
  - `test_sitemap_parse.cpp` hält das Widget fest, `test_json_squeeze.cpp` die Seite.

## Abweichungen zwischen Panel und openHAB

Diese Punkte sind bekannt, mit diesem Server reproduziert und nicht behoben. Jeder davon ist ein echter Fehler und kein Artefakt der Fixtures.

1. **`Frame` ergab eine leere Seite.** Behoben. openHAB verschachtelt die eigentlichen Widgets im eigenen Array `widgets` des Frames. Der Connector durchläuft jetzt eine Ebene tiefer: Die Kinder eines Frames werden in ihrer Reihenfolge zu Kacheln der Seite; der Frame selbst wird verworfen, da er keine eigene Kachel hat. Eine Ausnahme ist der Uhren-Frame der Hauptseite (`clock_frame`-Einstellung, standardmäßig `Clock`), dessen erste drei Kinder stattdessen zum Uhrbildschirm gehören. Die Seite **Framed** in `oheznav` dient weiterhin zur Reproduktion; beide Fälle werden von `test/host/main/test_sitemap_parse.cpp` festgehalten.
2. **Icon-URLs werden nicht prozentkodiert.** `Item::iconUrl()` fügt den Status unverändert in den Query-String ein, woraufhin `esp_http_client_set_url()` die URL direkt ablehnt. Sie erreicht nie den Server, und `session_prepare()` macht aus der Ablehnung einen Verbindungsabbruch. Bei jeder Icon-Aktualisierung wird daher eine Keep-alive-Verbindung abgebaut. Ein Leerzeichen verursacht das (`NAV_Spaced`), ebenso jedes Nicht-ASCII-Byte (`NAV_Umlaut` mit `Süd`). Zahlen sind nicht betroffen, weil der Parser sie zuerst über `%f` neu ausgibt. Eine Korrektur erfordert sowohl eine Kodierung als auch eine Vergrößerung von `STR_URL_LEN`: Ein Status mit maximal 32 Byte wächst im schlimmsten Fall auf 93 Byte an und bringt die längste zulässige URL auf 276 Zeichen – mehr als die 256, für die `test_item_urls.cpp` Platz garantiert.
3. **Der formatierte Wert von openHAB wird nur von Textkacheln verwendet.** Eine Kachel der Typen String, DateTime, Location oder Contact zeigt `[...]` aus der Beschriftung an, das vom Sitemap-Stream aktuell gehalten wird. Beim ersten Ereignis über den Elementstream fällt sie auf den rohen Status zurück. Eine Zahlenkachel formatiert weiterhin die Zahl selbst, sodass eine AVG-Gruppe `20.15` anzeigt, wo openHAB `20,2 °C` meldet.
4. **`Number:Dimensionless` ist in beide Richtungen falsch.** Bei einem gesendeten Befehl `"48 %"` speichert openHAB das Verhältnis `"0.48"`, und die Kachel zeigt `0 %`. Bei einem einfachen `48` speichert openHAB `"48"`; die Kachel zeigt `48 %`, während die eigene Beschriftung von openHAB `4800 %` ausgibt. Zur Behebung wird die Einheit benötigt, die das Panel verwirft.
5. **Beide Begrenzungen erfolgen stillschweigend.** Eine Seite mit zehn Widgets zeigt fünf Widgets und eine Zurück-Kachel; eine Selection mit zwölf Einträgen listet zehn auf. Auf dem Bildschirm ist nicht zu erkennen, dass etwas verworfen wurde. Ausgeblendete Widgets und vom Panel nicht darstellbare Widgettypen werden schon vor der Zählung übersprungen und belegen daher keinen Platz mehr.
6. **Wenn ein Server nach dem ersten Laden verschwindet, wird das nicht gemeldet.** Eine bearbeitete Sitemap erreicht das Panel jetzt über den Sitemap-Stream; ein Server, der jedoch *nach* dem ersten Laden ausfällt, bleibt unsichtbar: Der Stream verbindet sich still wieder, jede Abfrage schlägt still fehl, `page.state` bleibt `ready`, und es erscheint kein Banner. `SITEMAP ACCESS FAILED` wird nur bei einem Abruf einer *Seite* ausgelöst.

## Feste Limits des Panels

Diese Angaben sollte man zur Hand haben, wenn eine Seite erstellt wird, die in das Panel passen soll.

| Limit | Wert | Kennung |
| --- | --- | --- |
| Widgets pro Seite | 6, **einschließlich** der Zurück-Kachel | `ITEM_COUNT_MAX` |
| Einträge in einer Selection | 10 | `ITEM_SELECTION_COUNT_MAX` |
| Beschriftung | 32 Byte | `STR_LABEL_LEN` |
| Statustext | 32 Byte | `STR_STATE_TEXT_LEN` |
| Body einer Sitemap-Seite | 12288 Byte, nach dem Kürzen von Strings über 512 Byte | `OPENHAB_CLIENT_PAGE_BUFFER_SIZE`, `JSON_SQUEEZE_STRING_MAX` |
| Bild nach dem Dekodieren | höchstens halbe Bildschirmbreite und -höhe | `main/ui/items/item_image.cpp` |
| Icon-Body | 5000 Byte | `OPENHAB_CLIENT_ICON_BUFFER_SIZE` |
| Body von `/rest/sitemaps` | 8192 Byte | `OPENHAB_CLIENT_SITEMAPS_BUFFER_SIZE` |
| angebotene Sitemaps | 12 | `SITEMAP_LIST_COUNT_MAX` |
| bei einem mDNS-Scan aufgelistete Server | 8 | `OPENHAB_DISCOVER_COUNT_MAX` |
| gelesene mDNS-Antwort | 512 Byte | `MDNS_QUERY_PACKET_MAX` |

Eine echte Seite mit sechs Widgets umfasst etwa 3 KB JSON. Der Seitenpuffer ist daher nicht die Begrenzung, die es zunächst zu sein scheint.

## Das Panel damit steuern

[doc/test-interface_de.md](test-interface_de.md) ist die Referenz. Beim Durchlaufen eines tiefen Baums sind zwei Dinge besonders wichtig:

- **Mit `tap-tile 0` eine Seite nach oben gehen**, also die übergeordnete Link-Kachel verwenden. Nicht wischen: Das Panel unterstützt überhaupt keine Wischgesten. Bevor diese entfernt wurden, schien ein Wisch nur in einem Theme zu navigieren, weil er als Klick auf das Element ausgelöst wurde, das am Startpunkt der Wischbewegung lag.
- **Mit `tap 20 20` den Elementbildschirm verlassen**, und zwar über seine Zurück-Leiste.
- Nach jeder Navigation `wait-page` verwenden. `page.state` gibt den Ladezyklus an. Ein Tipp, bevor `ready` gemeldet wird, bewirkt überhaupt nichts.

## Fixtures entfernen

```bash
OH=/var/snap/openhab/current/conf
rm $OH/items/{oheztouch_gaps,oheznav}.items $OH/sitemaps/{oheztest,oheznav}.sitemap
```

`demo.*` gehört zum Server selbst; lass diese Dateien stehen.
