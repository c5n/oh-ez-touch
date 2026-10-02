# openHAB sitemaps

The panel builds its touch buttons and graphics dynamically. An openHAB
sitemap on the server defines the structure.

[The openHAB demo](openHAB/README.md) is a sitemap and items to install, with
a picture of every feature described here.

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

Widgets of other types (Chart, Image, Webview, Mapview, Input and so on) are
left out. So is a Switch over an item that is neither a Switch, a
Rollershutter, a Player nor a group of Switches or Rollershutters. A widget
that a `visibility=[...]` rule hides is left out too, and so is everything in
a hidden Frame. None of them takes one of the six places. When a rule shows or
hides a widget, the panel loads the page again.

An item whose state description is read-only, for example
`{ stateDescription=""[readOnly=true] }`, gets a tile that only shows its
state. A tap on it does nothing, even inside a Switch or a Slider.

## Value labels

A tile shows the label openHAB gives a state where there is one, and the raw
state where there is none. The labels come from the first of these that has
any:

1. the widget's `mappings=[...]` in the sitemap
2. the item's command options
3. the item's state options, for example `Number Mode "Mode" { stateDescription=""[options="1=Comfort,2=Eco"] }`

A Selection offers the same list as its choices. A Number is matched as a
number, so the option `2` matches the state `2.0`. The label is looked up on
the panel. A `MAP(...)` transformation or a date pattern in the label is
applied by openHAB, and a text tile shows the value openHAB formatted.

## Colours

`labelcolor`, `valuecolor` and `iconcolor` work as they do in Basic UI:

```
Text item=Outside_Temperature valuecolor=[<5="#4fc3f7", >25="orange"] iconcolor=[>25="red"]
```

openHAB evaluates the rule and sends the resulting colour. The panel draws the
name, the value or the icon in that colour on top of the theme. The names
openHAB lists (`maroon`, `red`, `orange`, `olive`, `yellow`, `purple`,
`fuchsia`, `pink`, `white`, `lime`, `green`, `navy`, `blue`, `teal`, `aqua`,
`black`, `silver`, `gray`, `gold`) and `#rrggbb` are understood. Anything else
keeps the theme's colour. The colours change with the state.

A widget with `staticIcon=<name>` in place of `icon=<name>` has the same icon
whatever its state. The panel fetches it once, without the state.

## Live updates

The panel follows the page on screen through openHAB's sitemap events. A
changed state, formatted value, colour or visibility reaches the panel
without a page load. If the sitemap file is edited, the page is loaded again.
A server that does not offer sitemap events (older than openHAB 3, or one
that requires a login for them) gets per-item state events instead. Then
colours and visibility change with the next page load.

An item with the tag `ohez-pin` asks for the Item PIN before its tile does
anything. See [PINs](configuration.md#pins).

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
