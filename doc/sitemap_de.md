# openHAB-Sitemaps

Das Bedienfeld erstellt seine Touch-Schaltflächen und Grafiken dynamisch. Eine
openHAB-Sitemap auf dem Server legt die Struktur fest.

[Die openHAB-Demo](openHAB/README.md) enthält eine zu installierende Sitemap
und Items sowie ein Bild zu jeder hier beschriebenen Funktion.

## Unterstützte Elemente

Sitemaps für OhEzTouch können folgende Elemente enthalten:

- Colorpicker
- Frame
- Image, für ein Image-Item
- Selection
- Setpoint
- Slider
- Switch
- Text
- Default

Eine Seite zeigt höchstens sechs Kacheln. Eine Unterseite zeigt fünf, da die
erste Kachel zum Zurückgehen dient.

Ein Colorpicker öffnet einen Bildschirm mit einem Farbfeld und drei Feldern:
Farbton, Sättigung und Wert. Jedes Feld zeigt die Farben, die es mit den
aktuellen Werten der anderen beiden Felder erreicht; während des Ziehens ändern
sich die Farben der Felder. Die neue Farbe wird gesendet, sobald der Finger
angehoben wird. **Off** sendet `OFF`, **Full** sendet die Farbe mit voller
Helligkeit.

Ein Slider bewegt sich in den Schritten seines `step`-Werts und sendet den Wert
mit der Genauigkeit des Schritts: `50` bei einem Schritt von 1, `21.5` bei
einem Schritt von 0.5. Ein Setpoint verhält sich genauso. Ein Wert, den etwas
anderes außerhalb des Schrittrasters gesetzt hat, wird beim nächsten Tippen
wieder auf das Raster gesetzt.

Ein Rollershutter (oder eine Gruppe davon) öffnet einen Bildschirm mit seiner
Position, einem Feld, das beim Anheben des Fingers eine Position von 0 (offen)
bis 100 (geschlossen) sendet, sowie AUF, STOP und AB.

Ein Switch mit `mappings` für ein Item, das kein Switch ist (eine Szene, ein
Modus, eine Lüfterstufe), öffnet denselben Bildschirm wie eine Selection: eine
Schaltfläche pro Mapping. Ebenso öffnet `Default` auf einem Number- oder
String-Item mit bis zu vier Befehlsoptionen einen solchen Bildschirm; openHAB
sendet diese Optionen als Switch.

Ein Image auf einem Image-Item zeigt das Bild des Items; siehe
[Bild: eine Türklingel oder eine Kamera](#bild-eine-türklingel-oder-eine-kamera).

Widgets anderer Typen (Chart, Webview, Mapview, Input usw.) werden
weggelassen. Das gilt auch für ein Image mit `url=` und ohne Item. Ebenso wird
ein Switch ohne Mappings weggelassen, wenn das Item weder ein Switch noch ein
Rollershutter, Player oder eine Gruppe aus Switches oder Rollershuttern ist,
beispielsweise ein Switch auf einem Dimmer. Auch ein Widget, das eine
`visibility=[...]`-Regel ausblendet, sowie alles in einem ausgeblendeten Frame
wird weggelassen. Keines davon belegt einen der sechs Plätze. Wenn eine Regel
ein Widget ein- oder ausblendet, lädt das Bedienfeld die Seite erneut.

Ein Item, dessen Zustandsbeschreibung schreibgeschützt ist, zum Beispiel
`{ stateDescription=""[readOnly=true] }`, erhält eine Kachel, die nur seinen
Zustand anzeigt. Tippen bewirkt nichts, auch nicht innerhalb eines Switches
oder Sliders.

## Bild: eine Türklingel oder eine Kamera

```
Image item=FrontDoor_Snapshot refresh=10000
```

Ein Türklingel- oder Kamera-Binding legt seinen Schnappschuss in einem
Image-Item ab. Der Zustand des Items ist das Bild selbst, daher zeigt die
Kachel den Namen und das Symbol, aber kein Bild. Durch Tippen wird das Bild so
groß geöffnet, wie es der Bildschirm zulässt. Durch Tippen auf das Bild wird
es erneut abgerufen; dasselbe gilt für `refresh=` (in Millisekunden, höchstens
alle zwei Sekunden) und für einen neuen Schnappschuss, der während der Anzeige
eintrifft.

Das Bedienfeld fragt das Bild unter `/rest/items/<item>/state` mit
`Accept: image/*` ab. Dadurch sendet openHAB das JPEG selbst statt des
gespeicherten Base64-Texts. Das Bedienfeld decodiert es während des Empfangs
und verkleinert es während der Decodierung auf höchstens die halbe
Bildschirmgröße; dargestellt wird es dann mit der doppelten Größe. Ein
640x480-Schnappschuss wird im Querformat zu 107x80. Das Bild ist unscharf, aber
ein Bild in voller Größe passt nicht in den Speicher eines Bedienfelds ohne
PSRAM. Unterstützt wird nur baseline JPEG; ein progressives JPEG oder PNG zeigt
„No picture“. Wenn das Binding einen kleineren Schnappschuss liefern kann,
fordern Sie diesen an: Er wird schneller geladen und sieht nicht schlechter
aus.

Das Bild kann auch selbstständig von jeder Seite aus geöffnet werden, wenn es
an der Tür klingelt. Siehe [Türklingel](configuration_de.md#türklingel).

## Player

openHABs Player-Item überträgt nur die Wiedergabesteuerung: PLAY, PAUSE, NEXT,
PREVIOUS. Ein Binding legt den Titel und die Lautstärke auf eigenen Items ab,
beispielsweise Sonos `currenttitle`, `currentartist` und `volume` oder Spotify
`trackName` und `artistName`. Fügen Sie diese gemeinsam mit dem Player in einen
Frame ein:

```
Frame label="Music"
{
    Default item=Sonos_Control
    Text    item=Sonos_Title
    Text    item=Sonos_Artist
    Slider  item=Sonos_Volume
}
```

Der Bildschirm des Players zeigt dann das erste Text-Item seines Frames als
Titel, bis zu zwei weitere darunter und das erste Slider-Item als
Lautstärkefeld unter den Wiedergabetasten. Die Werte folgen dem Server, solange
der Bildschirm geöffnet ist. Ein Player außerhalb eines Frames oder allein in
einem Frame erhält nur die Wiedergabetasten. Die Items des Frames sind auch
Kacheln der Seite und zählen daher zu den sechs Plätzen.

Ein untergeordneter Block direkt am Player (`Default item=... { ... }`) wäre
der naheliegende Weg, dies auszudrücken. Die openHAB-Sitemap-Syntax erlaubt ihn
jedoch nur für Text, Group, Image, Frame und Buttongrid.

Die Kachel des Players zeigt „Playing“ oder „Paused“.

## Wertbeschriftungen

Eine Kachel zeigt die Beschriftung, die openHAB einem Zustand zuordnet, sofern
es eine gibt, andernfalls den Rohzustand. Die Beschriftungen stammen aus dem
ersten Eintrag in dieser Liste, der Werte enthält:

1. `mappings=[...]` des Widgets in der Sitemap
2. Befehlsoptionen des Items
3. Zustandsoptionen des Items, zum Beispiel `Number Mode "Mode" { stateDescription=""[options="1=Comfort,2=Eco"] }`

Eine Selection bietet dieselbe Liste als Auswahlmöglichkeiten an. Ein Number
wird als Zahl abgeglichen, daher passt die Option `2` zum Zustand `2.0`. Die
Beschriftung wird auf dem Bedienfeld nachgeschlagen. Eine `MAP(...)`-Transformation
oder ein Datumsformat in der Beschriftung wird von openHAB angewendet; eine
Textkachel zeigt den von openHAB formatierten Wert.

## Farben

`labelcolor`, `valuecolor` und `iconcolor` funktionieren wie in der Basic UI:

```
Text item=Outside_Temperature valuecolor=[<5="#4fc3f7", >25="orange"] iconcolor=[>25="red"]
```

openHAB wertet die Regel aus und sendet die resultierende Farbe. Das Bedienfeld
zeichnet den Namen, den Wert oder das Symbol in dieser Farbe über dem Theme.
Die von openHAB aufgeführten Namen (`maroon`, `red`, `orange`, `olive`,
`yellow`, `purple`, `fuchsia`, `pink`, `white`, `lime`, `green`, `navy`,
`blue`, `teal`, `aqua`, `black`, `silver`, `gray`, `gold`) und `#rrggbb` werden
erkannt. Bei allen anderen Werten bleibt die Theme-Farbe erhalten. Die Farben
ändern sich mit dem Zustand.

Ein Widget mit `staticIcon=<name>` anstelle von `icon=<name>` behält unabhängig
von seinem Zustand dasselbe Symbol. Das Bedienfeld ruft es einmalig ohne den
Zustand ab.

## Live-Aktualisierungen

Das Bedienfeld verfolgt die angezeigte Seite über Sitemap-Ereignisse von
openHAB. Ein geänderter Zustand, formatierter Wert, eine geänderte Farbe oder
Sichtbarkeit erreicht das Bedienfeld ohne erneutes Laden der Seite. Wird die
Sitemap-Datei bearbeitet, wird die Seite erneut geladen. Ein Server, der keine
Sitemap-Ereignisse bereitstellt (älter als openHAB 3 oder mit erforderlicher
Anmeldung dafür), liefert stattdessen Zustandsereignisse pro Item. In diesem
Fall ändern sich Farben und Sichtbarkeit erst beim nächsten Laden der Seite.

Ein Item mit dem Tag `ohez-pin` verlangt vor jeder Aktion auf seiner Kachel die
Item-PIN. Siehe [PINs](configuration_de.md#pins).

## Frames

Das Bedienfeld zeichnet keine Überschriften. Die Items eines `Frame` werden in
Sitemap-Reihenfolge zu Kacheln der Seite, als wäre der Frame nicht vorhanden.
Es wird nur eine Ebene gelesen: Ein Frame innerhalb eines Frames wird
ignoriert.

### Der Uhren-Frame

Ein Frame auf der **Startseite** der Sitemap hat eine besondere Aufgabe. Wenn
seine Beschriftung mit der Einstellung **Clock items frame** (standardmäßig
`Clock`) übereinstimmt, werden seine ersten drei Items statt auf der Seite auf
dem Uhrenbildschirm angezeigt. Diesen zeigt das Bedienfeld beim Dimmen, wenn
**Show time and date when dimmed** aktiviert ist. Jedes Item wird als Symbol,
Wert und Name unter der Uhrzeit angezeigt, dort, wo sonst Wochentag und Datum
stehen.

```
sitemap home label="Home"
{
    Frame label="Clock"
    {
        Text   item=Outside_Temperature label="Outside"  icon="temperature"
        Text   item=Living_Humidity     label="Humidity" icon="humidity"
        Switch item=gLights             label="Lights"   icon="light"
    }

    Group item=gLivingRoom
    Text  label="Bedroom" icon="bedroom" { ... }
}
```

- Diese Items sind keine Kacheln. Sie zählen nicht zu den sechs Plätzen.
- Auf sie wird nur zugehört und sie werden nur abgefragt, solange der
  Uhrenbildschirm angezeigt wird.
- Der Beschriftungsvergleich ignoriert den Teil `[...]`; Groß- und
  Kleinschreibung wird beachtet.
- Ein Uhren-Frame auf einer Unterseite ist ein gewöhnlicher Frame. Beim Dimmen
  kehrt das Bedienfeld immer zur Startseite zurück; nur diese Seite wird also
  von der Uhr gelesen.
- Ohne passenden Frame zeigt der Uhrenbildschirm Wochentag und Datum.
- Der Wert wird so formatiert wie auf der Kachel, mit dem Zustandsmuster des
  Items und dem Mapping eines Switches oder einer Selection.

Siehe [Dimmen der LCD-Hintergrundbeleuchtung](configuration_de.md#lcd-hintergrundbeleuchtung-dimmen)
zu Farben und Übergang.

## Beispiel

Beispiel-Sitemap (`oheztouch.sitemap`):

```
sitemap oheztouch label="OhEzTouch Test"
{
    Switch      item=OHEZTOUCH_Switch
    Selection   item=OHEZTOUCH_Switch mappings=[OFF="Off", ON="On"]
    Selection   item=OHEZTOUCH_Select mappings=["SEL1"="Selection 1", "SEL2"="Selection 2", "SEL3"="Selection 3"]
    Default     item=OHEZTOUCH_Player
    Default     item=OHEZTOUCH_Rollershutter

    Text label="Submenu" icon="settings"
    {
        Setpoint    item=OHEZTOUCH_Number label="Setpoint"  minValue=-10 maxValue=10 step=0.5
        Slider      item=OHEZTOUCH_Number label="Slider"    minValue=-10 maxValue=10 step=1
        Text        item=OHEZTOUCH_Number label="Text"
        Colorpicker item=OHEZTOUCH_Color
    }
}
```

Items für die Beispiel-Sitemap:

```
String          OHEZTOUCH_Select        "Selection"         <fan>
String          OHEZTOUCH_String        "String"            <text>
Switch          OHEZTOUCH_Switch        "Switch"            <switch>
Number          OHEZTOUCH_Number        "Number [%.1f °C]"  <temperature>
Color           OHEZTOUCH_Color         "Color [%s]"        <colorlight>
Player          OHEZTOUCH_Player        "Player"            <receiver>
Rollershutter   OHEZTOUCH_Rollershutter "Rollershutter"     <blinds>
```

## Symbolgrößen

Einige der ursprünglichen openhab-webui-Symbole sind außergewöhnlich groß.
Der ESP32 hat nur begrenzten RAM. Codieren Sie die PNG-Dateien mit ImageMagick
neu:

```bash
sudo apt install imagemagick
git clone https://github.com/openhab/openhab-webui.git
cd openhab-webui/bundles/org.openhab.ui.iconset.classic/src/main/resources/icons
mkdir output
for f in *.png; do convert $f -strip output/$f; done
```

Verschieben Sie die Dateien aus dem Ordner `output` in Ihren Ordner
`openhab2-conf/icons/classic/`.

## Test-Fixtures

`test/openhab/` enthält Sitemaps und Items zum Testen mit einem echten Server.
Siehe [doc/openhab-fixtures.md](openhab-fixtures_de.md).
