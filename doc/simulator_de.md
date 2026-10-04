# Der Simulator

Die Benutzeroberfläche kann auf dem Entwicklungsrechner in einem SDL2-Fenster gebaut und ausgeführt werden. Hardware ist nicht erforderlich. Der Simulator verwendet denselben Quellcode, dieselbe LVGL-Version, dieselbe `lv_conf.h`, dieselben Schriftarten und dieselben Stile. Nur die Dateien in `main/port/` unterscheiden sich. Der Simulator ruft die Sitemap über HTTP ab, stellt seine Weboberfläche bereit und speichert seine Einstellungen wie das Panel.

## Kompilieren und ausführen

```bash
idf.py -B build/linux -DSDKCONFIG=build/linux/sdkconfig \
       -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.linux" \
       --preview set-target linux
idf.py -B build/linux build
./build/linux/oh-ez-touch.elf
```

Ein Fenster namens "OhEzTouch" wird geöffnet. Es zeigt einen 320x240-Bildschirm in doppelter Größe (`port_display_init()` in `main/port/linux/port_display.c`). Mausklicks werden als Touch-Eingabe behandelt. Beim Schließen des Fensters wird das Programm beendet.

## Headless-Betrieb

Auf einem Build-Rechner läuft kein Fensterserver, und ein Test, der nichts mit dem Bildschirm zu tun hat, sollte auf einem Desktop, auf dem einer läuft, kein Fenster öffnen. SDLs Dummy-Videotreiber deckt beide Fälle ab, ohne dass neu gebaut werden muss:

```bash
env -u DISPLAY SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
    ./build/linux/oh-ez-touch.elf
```

**Beide Variablen werden benötigt.** `lv_conf.h` setzt `LV_SDL_ACCELERATED`; LVGL fordert daher bei `SDL_CreateRenderer()` einen beschleunigten Renderer an, den der Dummy-Treiber nicht hat. Allein damit schlägt es mit *Couldn't find matching render driver* fehl, und `lv_sdl_window_create()` gibt NULL zurück. SDL berücksichtigt den Hinweis `SDL_RENDER_DRIVER` anhand des Namens, ohne die angeforderten Flags erneut zu prüfen, sodass die Angabe des Software-Renderers die Anforderung erfüllt. `offscreen` funktioniert genauso und bringt hier keinen zusätzlichen Vorteil.

Alles andere bleibt unverändert. LVGL rendert wie immer, sodass die unten beschriebene Steuerschnittstelle `screen` und `tap-label` normal beantwortet und `shot` einen echten Framebuffer zurückgibt – Screenshots werden als Pixel über HTTP übertragen und brauchten nie ein Fenster. Audio ist ein separates SDL-Subsystem, daher zeichnet `SDL_AUDIODRIVER=disk` die Töne weiterhin auf; `SDL_AUDIODRIVER=dummy` schaltet sie stumm.

Nicht verfügbar sind Maus und Tastatur: Es gibt kein Fenster, in das geklickt werden könnte. Ein Headless-Lauf wird über die Steuerschnittstelle bedient oder gar nicht.

## Unterschiede zum Panel

- **openHAB**: Lege den Server in der Konfigurationsdatei oder mit `OHEZ_OPENHAB_HOST`, `OHEZ_OPENHAB_PORT` und `OHEZ_SITEMAP` fest. Die Bedienung eines Widgets sendet wie auf dem Panel einen Befehl per POST.
- **Weboberfläche**: Der Simulator verwendet Port 8780 statt Port 80. Ein nicht privilegierter Prozess kann Port 80 nicht binden. `OHEZ_WEBUI_PORT` überschreibt den Port.
- **Einstellungen**: Gespeichert unter `$XDG_CONFIG_HOME/oh-ez-touch/config.json` (oder `~/.config/oh-ez-touch/config.json`). Die Datei kann von Hand bearbeitet werden. `OHEZ_CONFIG_DIR` verschiebt sie.
- **WLAN-Zugangsdaten**: Gespeichert in einem emulierten NVS-Abbild unter `$XDG_STATE_HOME/oh-ez-touch/flash.bin`. `OHEZ_STATE_DIR` verschiebt es. Zwei Simulatorinstanzen können sich ein Abbild nicht teilen. Die zweite Instanz meldet den Konflikt, statt das Abbild zu beschädigen.
- **Kein Funk, keine Hintergrundbeleuchtung, kein Sensor und kein OTA.** Diese Funktionen melden "there is none". Die Registerkarte WLAN liefert bei **Scan** eine vorgefertigte Liste. Der Sensor veröffentlicht nichts. `POST /update` antwortet mit 501.
- **Die Touchscreen-Kalibrierung arbeitet mit einem modellierten Panel.** SDL meldet exakt die Fensterkoordinate, sodass es nichts zu kalibrieren gibt. Die Maus meldet weiterhin genau diese Koordinate, und jeder `tap` landet dort, wo er angegeben ist. Nur der Kalibrierungsbildschirm fragt nach einem *Rohwert*; der Port beantwortet die Anfrage, indem er die Abbildung eines resistiven Panels rückwärts auf die Position der Berührung anwendet. `OHEZ_TOUCH_SKEW` verbiegt dieses Panel gegenüber der gerade geltenden Kalibrierung, sodass der Bildschirm eine tatsächliche Korrektur anzeigen kann. Siehe [Ein Panel, das nicht existiert](#ein-panel-das-nicht-existiert).
- **Der Summer funktioniert.** Der Simulator erzeugt die Impulsfolge des PWM-Kanals des Panels und spielt sie über SDL ab. Siehe [Das Panel hören](#das-panel-hören).

## Offline-Modus

`OHEZ_OFFLINE=1` stellt die Sitemap, Widget-Symbole und Bilder von Image-Elementen aus einkompilierten Testdaten anstelle des Netzwerks bereit:

```bash
OHEZ_OFFLINE=1 ./build/linux/oh-ez-touch.elf
```

Dadurch wird ein stabiles, reproduzierbares Bildschirmbild erzeugt. So kannst du an der Benutzeroberfläche arbeiten, ohne einen openHAB-Server zu benötigen.

Die Testdaten in `main/sim/sitemap_fixture.cpp` sind eine kleine Demo-Sitemap. Sie enthält eine Startseite mit zwei Unterseiten und deckt jeden unterstützten Widget-Typ außer Image ab. Sie durchläuft denselben Parser wie eine echte Serverantwort. Bearbeite die Datei, um eine bestimmte Sitemap nachzubilden. Elementzustände werden aus den Testdaten gelesen und nicht zurückgeschrieben. Die Bedienung eines Widgets ändert den Zustand nur lokal. Im Offline-Modus gibt es keinen Ereignisstrom: Am anderen Ende hört niemand zu, daher klingelt auch die Türglocke nie.

Jedes Image-Element zeigt offline dasselbe Bild: `doc/openHAB/doorbell.jpg`, das nur für das Linux-Ziel von `main/sim/image_fixture.cpp` einkompiliert wird. Um es anzuzeigen, verweise mit dem Widget einer Testseite auf ein Image-Element; um es zu ändern, generiere das Array in dieser Datei anhand der JPEG-Datei mit `xxd -i` neu.

### Widget-Symbole

Die Widget-Symbole sind nicht Teil dieses Repositorys. Der klassische openHAB-Symbolsatz steht unter der EPL-2.0. Dieses Projekt steht unter der GPL-3.0. Lade die Symbole einmal in deine Arbeitskopie:

```bash
tools/fetch_sim_icons.py
```

Das Skript lädt die von der Demo-Sitemap verwendeten Symbole herunter, rastert sie und schreibt `main/sim/icon_fixture_data.h`. Diese Datei wird von git ignoriert. Das Skript benötigt Netzwerkzugriff und eines von `inkscape`, `rsvg-convert` oder ImageMagick. Ohne die Symbole zeichnet der Offline-Modus die Widgets ohne Symbole. Die Firmware verhält sich bei einer fehlgeschlagenen Symbolanfrage genauso.

Um den Symbolsatz selbst anzusehen, verwende `tools/fetch_openhab_icons.py`:

```bash
tools/fetch_openhab_icons.py                    # all icons, 32x32, into openhab-icons/
tools/fetch_openhab_icons.py --size 64          # bigger
tools/fetch_openhab_icons.py light heating      # named icons and their state variants
```

Statusvarianten werden neben der Standardvariante abgelegt (`light.png`, `light-on.png`, `light-off.png`), so wie openHAB sie bereitstellt. Aus demselben Lizenzgrund wird das Ausgabeverzeichnis von git ignoriert.

## Umgebungsvariablen

Die Variablen `OHEZ_*` werden nach der Konfigurationsdatei angewendet. Die Konfigurationsdatei kann eingesehen werden, ohne sie zu bearbeiten. Die Variablen funktionieren auch auf dem Gerät, das lediglich keine Umgebung hat, aus der es sie auslesen könnte.

```bash
OHEZ_THEME=lcars ./build/linux/oh-ez-touch.elf
OHEZ_THEME=jarvis OHEZ_NIGHT=on ./build/linux/oh-ez-touch.elf
```

`OHEZ_THEME` akzeptiert `material`, `lcars`, `jarvis` oder `classic`. `OHEZ_NIGHT` akzeptiert `off`, `on` oder `auto`. Ein nicht erkannter Wert wählt den Standard. `OHEZ_NIGHT_FROM` und `OHEZ_NIGHT_TO` legen die Stunden des `auto`-Zeitraums fest (standardmäßig 22 und 6). `OHEZ_ORIENTATION` akzeptiert `landscape` oder `portrait`: Das Fenster wird mit 240x320 geöffnet und jedes Theme ordnet die Anzeige hochkant neu an.

```bash
OHEZ_ORIENTATION=portrait OHEZ_THEME=lcars ./build/linux/oh-ez-touch.elf
```

Eine Überschreibung wird auf die Einstellungen im Arbeitsspeicher angewendet und ist damit der Wert, den das nächste *Speichern* schreibt: Wird ein Lauf mit `OHEZ_ORIENTATION=portrait` gestartet und werden anschließend die Einstellungen gespeichert – über das Panel, das Webformular, die REST-API oder MQTT –, wird `portrait` in der Konfigurationsdatei abgelegt. Spätere Läufe starten dann ohne die Variable hochkant.

Die Uhr verwendet den *konfigurierten* GMT-Versatz, nicht die Zeitzone des Hosts. So würde es auch das Panel anzeigen. Die Sommerzeit folgt, wenn sie aktiviert ist, ebenfalls der EU-Regel statt der Regel des Hosts, sodass sich beide Übergänge anhand einer auf einen Sonntag im März oder Oktober gestellten Uhr prüfen lassen. `OHEZ_NIGHT=auto` kann beim Überschreiten der Grenze beobachtet werden.

`OHEZ_MQTT`, `OHEZ_MQTT_HOST`, `OHEZ_MQTT_PORT` und `OHEZ_MQTT_TOPIC` richten den MQTT-Client für einen Lauf auf einen Broker aus:

```bash
OHEZ_MQTT=on OHEZ_MQTT_HOST=localhost ./build/linux/oh-ez-touch.elf
```

`OHEZ_MQTT` akzeptiert `on` oder `off`. Alles außer `off` oder `0` aktiviert MQTT.

`OHEZ_BLE_FIXTURE=1` stellt einkompilierte BLE-Anzeigen anstelle von echtem Bluetooth bereit: einen iBeacon, einen Eddystone-UID, einen Eddystone-TLM-Frame desselben Senders und ein einfaches Gerät, dessen Name als Scan-Antwort eintrifft. Letzteres wird nur bereitgestellt, wenn die Einstellung **Active scan** aktiviert ist – so wie eine echte Scan-Antwort nur eintrifft, wenn danach gefragt wurde. Die Daten durchlaufen dieselben Parser wie eine echte Anzeige:

```bash
OHEZ_BLE_FIXTURE=1 ./build/linux/oh-ez-touch.elf
```

Die Testdaten sind standardmäßig deaktiviert. Die Messwerte werden an einen Broker veröffentlicht. Ein Simulator, der erfundene Daten in einen Anwesenheitsverlauf schreiben würde, wäre schlimmer als einer, der nichts tut.

`OHEZ_OUTPUTS=1` stattet den Simulator mit drei Relais und einer dreikanaligen Stimmungsbeleuchtung aus. Sie existieren nur als Protokollzeilen. Das genügt, um die MQTT-Themen auf einem Desktop gegen einen echten Broker zu testen.

`OHEZ_SETTINGS` öffnet beim Start den Einstellungsbildschirm auf der angegebenen Seite. Der Wert ist ein Abschnitt (`theme`, `audio`, `info`, `wlan`, `openhab`, `mqtt`, `sensors`, `device`, `touch`, `time`) oder ein Menü (`settings`, auch `index` und `system`):

```bash
OHEZ_SETTINGS=wlan ./build/linux/oh-ez-touch.elf
```

`OHEZ_ITEM` öffnet ein Bedienelement. Der Wert ist ein durch Punkte getrennter Pfad aus Kachelindizes. Jeder Schritt außer dem letzten folgt der mit dieser Kachel verknüpften Seite. Der letzte Schritt öffnet die Bedienung dieser Kachel. `5` öffnet die sechste Kachel der Startseite. `0.4` folgt der ersten Kachel und öffnet die fünfte Kachel der dahinterliegenden Seite:

```bash
OHEZ_ITEM=0.4 OHEZ_THEME=lcars OHEZ_NIGHT=on ./build/linux/oh-ez-touch.elf
```

`OHEZ_SETTINGS` und `OHEZ_ITEM` springen direkt zum Ziel und umgehen die System- und Element-PINs. Sie sind programmatische Einstiege wie `settings` und `nav` der Steuerschnittstelle.

`OHEZ_PIN_SYSTEM` und `OHEZ_PIN_ITEM` legen die beiden PINs beim Start fest, damit sich die Tastatur ausprobieren lässt, ohne zuvor manuell eine PIN einzurichten:

```bash
OHEZ_OFFLINE=1 OHEZ_PIN_SYSTEM=1234 OHEZ_PIN_ITEM=5678 ./build/linux/oh-ez-touch.elf
```

Anders als die übrigen Überschreibungen werden diese nicht nur auf den Arbeitsspeicher angewendet. Eine PIN liegt im NVS, das der Simulator in seinem emulierten Flash speichert. Eine auf diese Weise gesetzte PIN bleibt daher auch bei späteren Läufen ohne die Variable gesetzt. Entferne sie über die Geräteseite oder mit `tools/ohez_ctl.py pin system clear`. Die Offline-Testdaten versehen zwei Kacheln, **Living Room** und **All Lights**, mit `ohez-pin`, sodass die Element-PIN etwas zu schützen hat. Siehe [PINs](configuration_de.md#pins).

Auch im Simulator öffnet eine Berührung der Statusleiste den Einstellungsbildschirm. Auf dem Host gibt es kein Funkmodul, das konfiguriert werden könnte; daher öffnet sich der Bildschirm nie von selbst wie bei einem neuen Gerät.

## Ein Panel, das nicht existiert

Bei der Touchscreen-Kalibrierung werden vier Ecken berührt, die vom Panel gemeldeten Werte ausgelesen und Ursprung und Spannweite berechnet, mit denen der Zeiger umgerechnet wird. Ein Desktop hat weder Panel noch ADC. Der Simulator modelliert eines.

`port_indev_raw_press()` nimmt die Position der gerade von LVGL gelieferten Berührung und wendet rückwärts die Abbildung eines resistiven Panels darauf an. Das Ergebnis ist das 12-Bit-Paar, das ein solches Panel für diesen Finger gemeldet hätte. Sonst verwendet nichts das Modell: Der Zeiger meldet weiterhin die exakte Fensterkoordinate, sodass sich die Bedeutung vorhandener Skripte nicht ändert.

Das modellierte Panel verwendet die Konstanten des ArduiTouch. `OHEZ_TOUCH_SKEW` verbiegt es:

```bash
OHEZ_TOUCH_SKEW=1.04,-30,0.98,25 ./build/linux/oh-ez-touch.elf
```

Die vier Werte sind `xscale,xoffset,yscale,yoffset`. Die Skalierungsfaktoren multiplizieren die Spannweiten, die Offsets verschieben die Ursprünge. Die Zeile, die der Simulator beim Start protokolliert, zeigt, was er daraus gemacht hat:

```
touch: modelled panel 245/3764 289/3461 (calibration 275/3620 264/3532)
```

Ohne diese Variable entspricht das modellierte Panel der Kalibrierung, und das Verfahren meldet keinerlei Korrektur. Auch das ist ein eigener interessanter Fall: So sieht ein kalibriertes Panel aus.

Das gesamte Verfahren lässt sich mit einem Befehl durchlaufen:

```bash
tools/ohez_ctl.py calibrate-run
```

## Das Panel hören

Der Simulator erzeugt Ton. Siehe [Der Signalgeber](beeper_de.md), was er wiedergibt. `main/port/linux/port_beeper.c` öffnet ein SDL-Audiogerät und erzeugt die Impulsfolge, die der LEDC-Kanal des Panels erzeugen würde. Es gibt weder einen separaten Renderer noch eine zweite Kopie der Klangerzeugung.

Der Simulator folgt nicht den Aufrufen des Treibers an `port_beeper_tone()`. Stattdessen übernimmt er den gesamten Signalton und durchläuft selbst mit der Abtastrate die Frame-Funktion des gewählten Engines. Der Grund: Der FreeRTOS-Tick dieses Ziels beträgt 4 ms. Ein Interleave-Zeitfenster von zwei Millisekunden kann hier nicht von einer Task eingehalten werden. Würde man den Aufrufen folgen, wäre jeder Akkord viermal gröber gerendert als auf dem Panel.

Die *Parameter* des Engines (Tonhöhenverlauf, Hüllkurve, die beiden LFOs) werden auf dem eigenen Fünf-Millisekunden-Raster des Engines neu berechnet, nicht bei jeder Abtastung. Nur der Oszillator läuft schneller.

Der Oszillator wird pro ausgegebenem Sample achtmal abgetastet und gemittelt. Andernfalls würden Obertöne oberhalb der halben Abtastrate auf nicht zugehörige Frequenzen zurückgefaltet. Das Panel hat diesen Fehler nicht: LEDC steuert den Anschluss mit einer echten Rechteckwelle an, die nicht abgetastet wird.

Der Summer muss wie auf dem Panel in den Einstellungen aktiviert sein. Mit `SDL_AUDIODRIVER=disk` und `SDL_DISKAUDIOFILE` wird der Rohdatenstrom in eine Datei geschrieben, anstatt abgespielt zu werden.

**Was du damit beurteilen kannst**: Rhythmus, Kontur, Intervalle, ob zwei Signaltöne miteinander verwechselt werden können, ob einer länger dauert als die Geste, auf die er antwortet, ob die Stimmen eines Akkords miteinander kollidieren und wie deutlich das Interleave-Raster hörbar ist.

**Was du damit nicht beurteilen kannst, ist die Lautstärke.** Der Wandler des ArduiTouch hat eine scharfe mechanische Resonanz bei etwa 2–4 kHz. Nichts modelliert diese Resonanz, das Nachklingen nach dem Ende der Ansteuerung, das Gehäuse oder die Frequenzquantisierung von LEDC. Beurteile die Struktur im Simulator. Beurteile die Lautstärke auf einem Panel.

## Den Simulator skriptgesteuert bedienen

Der Simulator lauscht auf **127.0.0.1:8781** auf Befehle: eine Berührung oder Ziehbewegung senden, den Bildschirminhalt abfragen, Telemetriedaten auslesen oder den Framebuffer abrufen.

```bash
OHEZ_OFFLINE=1 ./build/linux/oh-ez-touch.elf &
tools/ohez_ctl.py wait-page
tools/ohez_ctl.py tap-label "Hallway Dimmer"
tools/ohez_ctl.py shot /tmp/panel.png --scale 2
```

`tools/ohez_ctl.py` ist der Client. Er benötigt nichts außer Python 3. Der Bildschirm wird als JSON zurückgegeben: welcher Bildschirm geöffnet ist und die Beschriftung, den Zustand, den Typ und das Rechteck jeder Kachel. Eine Prüfung kann einen Wert statt Pixeln überprüfen. Eine Kachel kann anhand ihrer Beschriftung statt anhand von Koordinaten berührt werden.

Screenshots werden als Rohpixel übertragen und auf dem Host in PNG umgewandelt. Das Panel kodiert keine PNG-Dateien, weil der Hardware der RAM für einen Encoder fehlt.

`OHEZ_TESTIF=0` deaktiviert den Socket. `OHEZ_TESTIF_PORT` ändert dessen Port. Nichts davon wird in die Firmware eines Panels einkompiliert.

**[doc/test-interface.md](test-interface_de.md) ist die vollständige Referenz**: die Befehlstabelle, das JSON, der Framebuffer-Header und die Fallstricke.

## Mit einem echten openHAB testen

Die einkompilierten Testdaten zeichnen einen Bildschirm ohne Server. Sie können nicht zeigen, was openHAB tatsächlich sendet. `test/openhab/` enthält Sitemaps und Elemente für einen echten Server. Siehe [doc/openhab-fixtures.md](openhab-fixtures_de.md) zur Installation und für Einzelheiten.
