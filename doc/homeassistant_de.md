# Home Assistant

Ein Home-Assistant-Build der Firmware zeigt statt einer openHAB-Sitemap ein
Lovelace-Dashboard von Home Assistant. Dieses Dokument beschreibt die
Einrichtung des Servers, wie das Panel ein Dashboard in Kacheln umsetzt und
was es nicht unterstützt. Wie die Firmware gebaut wird, steht unter
[Bauen](building_de.md#builds-für-home-assistant), wie der Connector intern
arbeitet, unter [Architektur](architecture_de.md#die-connector-grenze).

## Einrichtung

1. Bauen und flashen Sie ein Home-Assistant-Target, zum Beispiel
   `arduitouch_ha`.
2. Öffnen Sie in Home Assistant Ihr Benutzerprofil, Reiter **Sicherheit**, und
   erstellen Sie ein **langlebiges Zugriffstoken**. Kopieren Sie es; Home
   Assistant zeigt es nur einmal an.
3. Legen Sie ein Dashboard für das Panel an: **Einstellungen > Dashboards >
   Dashboard hinzufügen**, „Neues Dashboard von Grund auf“. Merken Sie sich
   seine URL, zum Beispiel `ohez-panel` in
   `http://homeassistant:8123/ohez-panel`.
4. Öffnen Sie auf dem Panel oder in seiner Weboberfläche den Abschnitt
   **Home Assistant** und tragen Sie ein:

   | Einstellung | Wert |
   | --- | --- |
   | Host | die Adresse von Home Assistant, am besten in Ziffern (`192.168.1.10`) |
   | Port | `8123` |
   | Access token | das Token aus Schritt 2 |
   | Dashboard | der URL-Teil aus Schritt 3, leer für das Standard-Dashboard |

Das Panel verbindet sich mit `ws://<host>:<port>/api/websocket`. Es verwendet
nur unverschlüsseltes HTTP: Ein Home Assistant, der nur über HTTPS erreichbar
ist (zum Beispiel über Nabu Casa), funktioniert nicht. Der ESP32 hat neben dem
Display keinen Heap für TLS.

Das Standard-Dashboard funktioniert erst, nachdem es einmal bearbeitet wurde
(„Kontrolle übernehmen“): Solange Home Assistant es selbst erzeugt, speichert
es keine Konfiguration, und das Panel meldet im Log „Home Assistant has no
configuration for dashboard“. Ein eigenes kleines Dashboard für das Panel ist
in jedem Fall zu empfehlen.

## Vom Dashboard zu den Kacheln

Eine Seite zeigt höchstens sechs Kacheln. Jede Ansicht (View) des Dashboards
ist eine Seite. Die erste Ansicht ist die Startseite.

Das Panel liest die Karten einer Ansicht in ihrer Reihenfolge und erzeugt
Kacheln, bis sechs voll sind:

| Karte | Kacheln |
| --- | --- |
| `tile`, `button`, `entity`, `light`, `thermostat`, `media-control`, `picture-entity` und jede andere Karte mit einer `entity` | eine Kachel für die Entität |
| `entities`, `glance` | eine Kachel pro Zeile, die eine Entität nennt |
| `vertical-stack`, `horizontal-stack`, `grid` | die enthaltenen Karten, zwei Ebenen tief |
| eine Karte mit `tap_action: navigate` und ohne Entität | eine Link-Kachel zu dieser Ansicht |
| `heading`, `markdown` und andere Karten ohne Entität | keine |

Eine Ansicht vom Typ `sections` funktioniert genauso: die Karten aller
Abschnitte in ihrer Reihenfolge.

Der `name` einer Karte ersetzt auf der Kachel den Anzeigenamen der Entität.

Auf der Startseite liefert eine `entities`- oder `glance`-Karte, deren Titel
der Einstellung **Clock frame** entspricht (Standard `Clock`), die
Uhr-Elemente (bis zu drei) statt Kacheln – wie der Uhr-Frame einer
openHAB-Sitemap.

Die Startseite verlinkt die anderen Ansichten im Platz, den die Karten frei
lassen. Eine als **Unteransicht** (subview) markierte Ansicht bekommt dort
keinen Link; sie wird über einen Button (`tap_action: navigate`) erreicht.
Jede Seite unterhalb der Startseite beginnt mit einer Zurück-Kachel.

Beispiel:

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

## Entitäten

Domäne und Attribute einer Entität bestimmen den Kacheltyp:

| Domäne | Kachel | Ein Tippen oder Bedienelement sendet |
| --- | --- | --- |
| `light` mit Farbe | Farbwähler | `light.turn_on` mit `hs_color` und `brightness_pct`, `light.turn_off` |
| `light`, das dimmt | Schieberegler (0–100 %) | `light.turn_on` mit `brightness_pct`, `light.turn_off` bei 0 |
| `light`, das nur schaltet, `switch`, `input_boolean`, `fan`, `siren`, `humidifier`, `automation`, `remote` | Schalter | `turn_on`, `turn_off` |
| `lock` | Schalter (an = verriegelt) | `lock.lock`, `lock.unlock` |
| `valve` | Schalter (an = offen) | `valve.open_valve`, `valve.close_valve` |
| `scene`, `script` | Schalter, immer aus | `turn_on` |
| `button`, `input_button` | Schalter, immer aus | `press` |
| `cover` | Rollladen | `open_cover`, `close_cover`, `stop_cover`, `set_cover_position` |
| `climate`, `water_heater` | Sollwert (Bereich und Schrittweite von der Entität) | `set_temperature` |
| `number`, `input_number` | Schieberegler, im Modus `box` Sollwert | `set_value` |
| `select`, `input_select` | Auswahl | `select_option` |
| `media_player` | Player | `media_play`, `media_pause`, `media_next_track`, `media_previous_track` |
| `sensor` mit einer Zahl | Zahl mit Einheit, nur lesend | nichts |
| alles andere (`binary_sensor`, Textsensoren, `person`, `weather`, ...) | Text, nur lesend | nichts |

Eine Rollladen-Kachel zählt wie openHAB: 0 ist offen, 100 ist geschlossen.
Die Position von Home Assistant läuft umgekehrt; das Panel rechnet sie um.

Ein `binary_sensor` zeigt „Open“/„Closed“ für Türen und Fenster,
„Detected“/„Clear“ für Bewegung und Anwesenheit und sonst „On“/„Off“.

Zustände werden übertragen: Das Panel abonniert die Entitäten der angezeigten
Seite (`subscribe_entities`) und zeigt eine Änderung, sobald Home Assistant
sie meldet. Eine Änderung des Dashboards in Home Assistant lädt die Seite neu.

## Icons

Home Assistant liefert keine Icon-Bilder. Das Panel zeichnet die Icons seines
eingebauten Satzes (siehe [Icon-Satz](icon-set_de.md)): nach Domäne und
Geräteklasse der Entität (eine Lampe, ein Temperatursensor, ein Garagentor),
mit dem Zustand, wo der Satz Varianten hat (Lampe an oder aus, Dimmstufe,
Rollladenposition). Das `icon` einer Karte wird verwendet, wenn es einer der
gängigen `mdi:`-Namen ist, die das Panel kennt (`mdi:lightbulb`,
`mdi:television`, `mdi:sofa`, ...). Ohne eingebauten Satz zeigen die Kacheln
das Platzhalter-Icon.

## Grenzen

- Nur unverschlüsseltes HTTP und WebSocket, kein TLS.
- Sechs Kacheln pro Seite, drei Uhr-Elemente. Karten nach der sechsten Kachel
  werden nicht angezeigt.
- Kein Bild einer `camera`- oder `image`-Entität und keine Türklingel.
- Keine bedingten Karten und keine Sichtbarkeitsregeln: Eine Karte wird
  angezeigt, ob ihre Bedingungen erfüllt sind oder nicht.
- Kartenfunktionen (etwa ein Helligkeitsregler in einer `tile`-Karte) und
  Kartenfarben werden nicht gelesen.
- Höchstens acht Ansichten erhalten Link-Kacheln auf der Startseite.
- Die Meldung für eine fehlgeschlagene Seite lautet weiterhin
  „SITEMAP ACCESS FAILED“, wie in einem openHAB-Build.
