# openHAB sitemaps

The panel builds its touch buttons and graphics dynamically. An openHAB
sitemap on the server defines the structure.

## Supported elements

Sitemaps for the OhEzTouch can contain these elements:

- Colorpicker
- Selection
- Setpoint
- Slider
- Switch
- Text
- Default

## Example

Example sitemap (`oheztouch.sitemap`):

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

Items for the example sitemap:

```
String          OHEZTOUCH_Select        "Selection"         <fan>
String          OHEZTOUCH_String        "String"            <text>
Switch          OHEZTOUCH_Switch        "Switch"            <switch>
Number          OHEZTOUCH_Number        "Number [%.1f °C]"  <temperature>
Color           OHEZTOUCH_Color         "Color [%s]"        <colorlight>
Player          OHEZTOUCH_Player        "Player"            <receiver>
Rollershutter   OHEZTOUCH_Rollershutter "Rollershutter"     <blinds>
```

## Icon sizes

Some of the original openhab-webui icons are exceptionally large. The ESP32
has limited RAM. Re-encode the PNG files with ImageMagick:

```bash
sudo apt install imagemagick
git clone https://github.com/openhab/openhab-webui.git
cd openhab-webui/bundles/org.openhab.ui.iconset.classic/src/main/resources/icons
mkdir output
for f in *.png; do convert $f -strip output/$f; done
```

Move the files from the `output` folder to your
`openhab2-conf/icons/classic/` folder.

## Test fixtures

`test/openhab/` holds sitemaps and items for testing against a real server.
See [doc/openhab-fixtures.md](openhab-fixtures.md).
