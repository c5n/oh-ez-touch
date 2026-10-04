# Der integrierte Icon-Satz

Früher lud das Panel jedes Widget-Icon per HTTP von openHAB: ein GET pro Kachel bei jedem Laden einer Seite und ein weiterer Aufruf, wenn sich der Zustand eines Elements änderte und damit auch das aufzulösende Icon. Eine Seite mit sechs Kacheln verursachte also sieben Anfragen, bevor sie gezeichnet wurde. Sechs davon galten Grafiken, die auf jeder openHAB-Installation weltweit identisch sind und sich ungefähr einmal pro Release ändern.

Diese Icons sind jetzt in der Firmware enthalten. Zuerst wird `icon_set_get()` abgefragt; nur bei einem Fehlschlag wird eine Anfrage gesendet.

## Generierung

Die Grafiken **sind nicht in diesem Repository enthalten**. Der klassische Icon-Satz von openHAB ist urheberrechtlich Eigentum des openHAB-Projekts und unter EPL-2.0 lizenziert. Diese Lizenz lässt sich nicht mit der GPL-3.0 dieses Projekts vereinbaren, deshalb wird die generierte Datei von Git ignoriert und jede Arbeitskopie erstellt eine eigene:

```sh
tools/build_icon_set.py            # the whole set (~400 icons), 32x32
idf.py reconfigure                 # usually unnecessary; see below
```

Dadurch wird `main/icons/icon_set_data.h` erzeugt. Ohne diese Datei geht nichts kaputt: Jede Suche findet nichts, und jedes Icon kommt von openHAB – genauso wie vor Einführung dieser Funktion. `idf.py` gibt beim Konfigurieren aus, welcher der beiden Fälle vorliegt:

```
-- oh-ez-touch: built-in icon set present (icon_set_data.h, 651552 bytes of source)
-- oh-ez-touch: no built-in icon set -- icons come from openHAB over HTTP; ...
```

Dieselbe Information erscheint beim Start auch auf der Konsole des Panels:

```
I (1234) openhab_client: built-in icons: 396, 228096 bytes
```

`main/CMakeLists.txt` überwacht `main/icons/` über `CMAKE_CONFIGURE_DEPENDS`. Normalerweise reicht es deshalb, den Header zu erstellen; beim nächsten Build wird er mitkompiliert. Das explizite `idf.py reconfigure` ist für den Fall gedacht, dass die Überwachung nicht greift. Der Fehler bleibt unbemerkt – der Build ruft weiterhin Icons über HTTP ab –, daher sollte man die Konfigurationsausgabe prüfen, statt es einfach anzunehmen.

Nützliche Argumente:

| | |
|---|---|
| `tools/build_icon_set.py light heating` | nur diese Icons, einschließlich Zustandsvarianten |
| `--size 24` | eine andere Kantenlänge (Standard 32, die Zeichengröße der Kacheln) |
| `--colors 8` | eine kleinere Palette |
| `--verify-all` | jedes Icon mit ImageMagick gegenprüfen, nicht nur das erste |

Den Satz zu kürzen ist der entscheidende Hebel für den Flash-Speicher, und bei dieser Firmware macht das viel aus: Der vollständige Satz umfasst 396 Icons und 228 kB Bilddaten. Auf einem 4-MB-Board bleiben in der 1,9-MB-App-Partition damit nur wenige Prozent frei. Ein Panel zeigt nur Icons an, die in seinen eigenen Sitemaps benannt sind; wenn diese explizit angegeben werden, schrumpft der Satz auf wenige kB.

## Warum ein 16-Farben-LVGL-Bild

Die klassischen Icons sind Strichzeichnungen: einige wenige Vollfarben und geglättete Kanten. Sechzehn Farben reichen dafür bequem aus. Die Palette wird überwiegend für Alphastufen und nicht für Farbtöne verwendet; deshalb arbeitet der Quantisierer mit RGBA statt RGB.

Der Datensatz ist ein **indiziertes LVGL-Bild**, keine PNG-Datei: sechzehn Paletteneinträge in `lv_color32_t`-Reihenfolge, danach die Pixel zu je zwei pro Byte gepackt; das ergibt 576 Byte pro Icon. Das beansprucht pro Icon gut hundert Byte *mehr* Flash als die indizierte PNG-Datei, die es ersetzt. Eingespart wird stattdessen RAM: Eine PNG muss vor dem Zeichnen dekodiert werden. Die Dekodierung benötigt den Arbeitsspeicher von lodepng sowie 4096 Byte für ein ARGB8888-Bild pro Kachel – auf einem Heap, der auch BLE und MQTT bedient. Auf Panels, die tagelang liefen, führte das zu folgendem Fehlerbild: „Die Kacheln werden gezeichnet, aber die Icons sind Platzhalter.“ Ein indizierter Datensatz kann vom LVGL-Renderer direkt als Palette und Pixel eingelesen werden – keine Dekodierung, kein vorübergehender Speicherbedarf, und der gesamte RAM-Bedarf eines Icons beträgt 576 Byte.

Vollständig transparente Pixel erhalten einen eigenen Paletteneintrag und werden bei der Quantisierung nie mit anderen Pixeln gruppiert. Ohne diese Maßnahme würde sich transparentes Schwarz mit dem Rand vermischen, sodass jedes Icon einen leichten Schein bekäme.

## Größere Server-Icons

Alles wird in einer Größe von 32 Pixeln gezeichnet, doch ein benutzerdefiniertes Icon unter `$OPENHAB_CONF/icons/classic/` kann 64 oder 128 Pixel groß sein. Statt die integrierten Icons zu vergrößern, wird eine abgerufene PNG-Datei, die nach dem Dekodieren größer als die Satzgröße ist, **in beiden Richtungen halbiert, bis sie hineinpasst**. Dazu wird ein Box-Mittelwert mit vormultipliziertem Alphakanal verwendet, damit ein transparenter Hintergrund nicht in die Striche verschmiert. Die halbierte Kopie wird von der Kachel gespeichert: 4096 Byte für ein 64-Pixel-Icon statt der sechzehn Kilobyte, die bei der Dekodierung entstanden.

## Was aufgelöst wird und was nicht

`icon_set_get()` wendet die drei eigenen Regeln von openHAB der Reihe nach an:

1. das zustandsspezifische Icon – `light-on` für `light` im Zustand `ON`;
2. bei einem numerischen Zustand die nächstgelegene Variante mit einem Wert kleiner oder gleich diesem – für `light` bei 44, dessen Stufen von `light-0` bis `light-100` in Zehnerschritten gehen, wird `light-40` verwendet;
3. das einfache Icon.

Regel 2 ist entscheidend und keine bloße Zugabe. Sobald der integrierte Satz zuerst antwortet, sieht das Panel dessen Regeln. Eine Suche, die nur die Regeln 1 und 3 kennt, würde jeden Dimmer mit voller Helligkeit anzeigen – obwohl der Server das bisher richtig gemacht hat.

Außerdem gibt es dabei eine Falle, die `test/host/main/test_icon_set.cpp` verhindern soll. Eine Stufe muss aus *Ziffern* bestehen: Für `light` gibt es neben der numerischen Reihe auch `light-off` und `light-on`, und `strtoul("off")` ergibt 0. Bei `light` bleibt dieser Fehler verborgen, weil 0 gegenüber `light-20` verliert und der Aufrufer ihn nie sieht. Bei den zwanzig Familien mit benannten, aber keinen numerischen Varianten – `contact`, `door`, `lock`, `presence` – gibt es keine Alternative, gegen die der falsche Wert verlieren könnte. Jeder numerische Zustand wird daher zu `contact-ajar` aufgelöst.

Bei einem Fehlschlag wird die Suche über HTTP fortgesetzt, wodurch benutzerdefinierte Icons weiterhin funktionieren: Alles, was unter `$OPENHAB_CONF/icons/classic/` abgelegt wird, existiert nur auf diesem einen Server und kann daher nicht in die Firmware eingebaut worden sein.

## Wo die Reihenfolge festgelegt ist

In `perform()` in `main/openhab/openhab_client.cpp`, bewusst nicht in der UI. Ein integriertes und ein abgerufenes Icon unterscheiden sich dadurch in nichts, was der Aufrufer sehen kann: gleiche Übergabe, gleiche Generation, gleiche Warteschlange, dieselben Bilddaten im selben Ergebnis. Die Kachel erhält ihre Antwort somit bei der nächsten Schleifenrunde statt nach einem Netzwerk-Rundlauf. Die UI weiß nicht, welcher Weg verwendet wurde: Ein Datensatz wird anhand seiner Größe und daran erkannt, dass er *nicht* mit dem PNG-Byte 0x89 beginnt. Dann wird er kopiert und gezeichnet; bei einer PNG-Datei erfolgt die Dekodierung.

Der Satz hat außerdem Vorrang vor den Beispieldaten des Offline-Modus. Dessen sechzehn Icons (`tools/fetch_sim_icons.py`, `main/sim/icon_fixture.cpp`) sind eine Teilmenge derselben Grafiken. Wird der integrierte Satz generiert, stehen die Icons somit auch dem Simulator zur Verfügung, online wie offline. Die Beispieldaten werden nur noch benötigt, wenn eine Arbeitskopie den einen, aber nicht den anderen Satz generiert hat.

## Tests

`test/host/main/test_icon_set.cpp` gehört zur Host-Suite – `cd test/host && idf.py
build && ./build/oh-ez-touch-host-test.elf`.

Die Regeln werden anhand von `test/host/main/icon_set_fixture.h` getestet: vierzehn Einträge, die wie die generierte Tabelle aufgebaut sind, nicht anhand des echten Satzes. Das ist notwendig: Die Grafiken sind nicht im Repository enthalten. Auf einem frischen Klon oder einem CI-Runner gibt es daher keine generierte Tabelle für den Test. Eine Suite, die sich dort stillschweigend selbst überspränge, würde genau die Konfiguration testen, die niemand ausliefert. `icon_set.cpp` erhält `ICON_SET_DATA_HEADER`, um auf die Beispieldatei zu verweisen; die Firmware definiert dieses Makro nie.

Die Korrektheit der generierten Tabelle wird vom Generator selbst geprüft: `--verify-all` dekodiert jedes Icon erneut mit ImageMagick statt mit dem eigenen Decoder des Encoders. So können sich Fehler, die an beiden Enden auftreten, nicht gegenseitig bestätigen.

## Deaktivieren

`CONFIG_OHEZ_ICONS_BUILTIN` (OhEzTouch → *Widget-Icons aus dem in der Firmware integrierten Satz beantworten*, Standardwert y). Wird die Option ausgeschaltet, ruft die Firmware wieder alle Icons ab, ohne den generierten Header zu löschen. So lässt sich messen, wie viel Flash-Speicher der Satz kostet und wie viele Anfragen er einspart.

## Die beiden anderen Icon-Werkzeuge

| Werkzeug | Ausgabe | Zweck |
|---|---|---|
| `tools/build_icon_set.py` | `main/icons/icon_set_data.h` | integrierter Icon-Satz der Firmware |
| `tools/fetch_openhab_icons.py` | `openhab-icons/*.png` | nachsehen, welches Icon zu einem Namen aufgelöst wird |
| `tools/fetch_sim_icons.py` | `main/sim/icon_fixture_data.h` | die sechzehn Beispieldaten des Offline-Modus |

Alle drei rufen Daten von derselben Stelle ab und verwenden denselben Code zum Auflisten, Herunterladen und Rastern; sie unterscheiden sich nur darin, was sie schreiben. Siehe auch `doc/openhab-fixtures.md`, wo die Unterschiede zwischen diesem Panel und einem echten openHAB aufgeführt sind – darunter, dass die Icons PNG- statt SVG-Dateien sind.
