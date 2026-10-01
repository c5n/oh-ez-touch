# openHAB sitemaps

The panel builds its touch buttons and graphics dynamically. An openHAB
sitemap on the server defines the structure.

## Supported elements

Sitemaps for the OhEzTouch can contain these elements:

- Colorpicker
- Frame
- Selection
- Setpoint
- Slider
- Switch
- Text
- Default

A page shows at most six tiles. A sub page shows five, because the first tile
is the way back.

A Colorpicker opens a screen with a swatch and three fields: hue, saturation
and value. Each field shows the colours it reaches with the other two held
where they are, and the fields recolour as you drag. The new colour is sent
when the finger lifts.

## Frames

The panel draws no headings. A `Frame`'s items become tiles of the page, in
sitemap order, as if the Frame were not there. Only one level is read: a Frame
inside a Frame is ignored.

### The clock frame

One Frame on the sitemap's **home page** has a job of its own. When its label
matches the **Clock items frame** setting (`Clock` by default), its first
three items go to the clock screen instead of to the page. The clock screen is
what the panel shows when it dims, if **Show time and date when dimmed** is
on. It shows each item as icon, value and name under the time, where the
weekday and the date would otherwise be.

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

- These items are not tiles. They do not count against the six.
- They are listened for, and polled, only while the clock screen is up.
- The label is compared without its `[...]` part, and case matters.
- A clock frame on a sub page is an ordinary Frame. The panel always goes back
  to the home page when it dims, so that is the only page the clock reads.
- Without a matching Frame, the clock screen shows the weekday and the date.
- The value is formatted the way the tile would show it, with the item's state
  pattern and the mapping of a Switch or Selection.

See [LCD Backlight Dimming](configuration.md#lcd-backlight-dimming) for the
colours and the transition.

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
