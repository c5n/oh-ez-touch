# Home Assistant

A Home Assistant build of the firmware shows a Lovelace dashboard of Home
Assistant instead of an openHAB sitemap. This document describes how to set
up the server, how the panel turns a dashboard into tiles, and what it does
not support. How to build the firmware is in
[Building](building.md#home-assistant-builds), how the connector works inside
in [Architecture](architecture.md#the-connector-boundary).

## Setup

1. Build and flash a Home Assistant target, for example `arduitouch_ha`.
2. In Home Assistant, open your user profile, tab **Security**, and create a
   **long-lived access token**. Copy it; Home Assistant shows it only once.
3. Create a dashboard for the panel: **Settings > Dashboards > Add
   dashboard**, "New dashboard from scratch". Note its URL, for example
   `ohez-panel` in `http://homeassistant:8123/ohez-panel`.
4. On the panel or in its web interface, open the **Home Assistant** section
   and set:

   | Setting | Value |
   | --- | --- |
   | Host | the address of Home Assistant, digits recommended (`192.168.1.10`) |
   | Port | `8123` |
   | Access token | the token from step 2 |
   | Dashboard | the URL part from step 3, empty for the default dashboard |

The panel connects to `ws://<host>:<port>/api/websocket`. It uses plain HTTP
only: a Home Assistant that is reachable through HTTPS only (for example
through Nabu Casa) does not work. The ESP32 has no heap for TLS next to the
display.

The default dashboard works only after it was edited once ("take control"):
while Home Assistant generates it itself, it keeps no configuration, and the
panel logs "Home Assistant has no configuration for dashboard". A separate
small dashboard for the panel is recommended in any case.

## From a dashboard to tiles

A page shows at most six tiles. Each view of the dashboard is one page. The
first view is the home page.

The panel reads the cards of a view in their order and makes tiles until six
are full:

| Card | Tiles |
| --- | --- |
| `tile`, `button`, `entity`, `light`, `thermostat`, `media-control`, `picture-entity` and every other card with an `entity` | one tile for the entity |
| `entities`, `glance` | one tile per row that names an entity |
| `vertical-stack`, `horizontal-stack`, `grid` | the cards inside it, two levels deep |
| a card with `tap_action: navigate` and no entity | a link tile to that view |
| `heading`, `markdown` and other cards without an entity | none |

A view of type `sections` works the same way: the cards of all sections, in
order.

A card's `name` replaces the entity's friendly name on the tile.

On the home page, an `entities` or `glance` card whose title is the
**Clock frame** setting (default `Clock`) gives the clock items (up to three)
instead of tiles, as the clock frame of an openHAB sitemap does.

The home page links to the other views in the room the cards leave free. A
view marked as **subview** gets no link there; reach it with a button
(`tap_action: navigate`). Every page below the home page starts with a back
tile.

Example:

```yaml
title: Panel
views:
  - title: Home
    path: home
    cards:
      - type: tile
        entity: light.ceiling_lights
      - type: button
        entity: switch.decorative_lights
        name: Deko
      - type: entities
        title: Clock
        entities: [sensor.outside_temperature, sensor.outside_humidity]
      - type: tile
        entity: cover.living_room_window
      - type: thermostat
        entity: climate.heatpump
      - type: button
        name: Media
        tap_action:
          action: navigate
          navigation_path: /ohez-panel/media
  - title: Media
    path: media
    subview: true
    cards:
      - type: media-control
        entity: media_player.living_room
      - type: tile
        entity: select.speed
```

## Entities

The entity's domain and attributes decide the tile type:

| Domain | Tile | A tap or control sends |
| --- | --- | --- |
| `light` with colour | colour picker | `light.turn_on` with `hs_color` and `brightness_pct`, `light.turn_off` |
| `light` that dims | slider (0–100 %) | `light.turn_on` with `brightness_pct`, `light.turn_off` at 0 |
| `light` that only switches, `switch`, `input_boolean`, `fan`, `siren`, `humidifier`, `automation`, `remote` | switch | `turn_on`, `turn_off` |
| `lock` | switch (on = locked) | `lock.lock`, `lock.unlock` |
| `valve` | switch (on = open) | `valve.open_valve`, `valve.close_valve` |
| `scene`, `script` | switch, always off | `turn_on` |
| `button`, `input_button` | switch, always off | `press` |
| `cover` | rollershutter | `open_cover`, `close_cover`, `stop_cover`, `set_cover_position` |
| `climate`, `water_heater` | setpoint (range and step from the entity) | `set_temperature` |
| `number`, `input_number` | slider, or setpoint in mode `box` | `set_value` |
| `select`, `input_select` | selection | `select_option` |
| `media_player` | player | `media_play`, `media_pause`, `media_next_track`, `media_previous_track` |
| `sensor` with a number | number with the unit, read only | nothing |
| everything else (`binary_sensor`, text sensors, `person`, `weather`, ...) | text, read only | nothing |

A rollershutter tile counts as openHAB does: 0 is open, 100 is closed. Home
Assistant's position is the other way round; the panel converts it.

A `binary_sensor` reads "Open"/"Closed" for doors and windows,
"Detected"/"Clear" for motion and presence, and "On"/"Off" otherwise.

States are pushed: the panel subscribes to the entities of the page on screen
(`subscribe_entities`) and shows a change when Home Assistant reports it. A
change of the dashboard in Home Assistant reloads the page.

## Icons

Home Assistant serves no icon pictures. The panel draws the icons of its
built-in set (see [Icon set](icon-set.md)): by the entity's domain and device
class (a light, a temperature sensor, a garage door), with the state where the
set has variants (a light on or off, a dimmer's level, a shutter's position).
A card's `icon` is used when it is one of the common `mdi:` names the panel
knows (`mdi:lightbulb`, `mdi:television`, `mdi:sofa`, ...). Without a built-in
set, the tiles show the placeholder icon.

## Limits

- Plain HTTP and WebSocket only, no TLS.
- Six tiles per page, three clock items. Cards past the sixth tile are not
  shown.
- No picture of a `camera` or `image` entity, and no doorbell.
- No conditional cards and no visibility rules: a card is shown whether its
  conditions hold or not.
- Card features (such as a brightness slider inside a `tile` card) and card
  colours are not read.
- At most eight views give link tiles on the home page.
- The failed-page box still says "SITEMAP ACCESS FAILED", as in an openHAB
  build.
