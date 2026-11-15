# Schriftarten

Die LVGL-Schriftquellen in `components/lvgl/fonts/` werden generiert und eingecheckt. Für einen normalen Build werden keine Schriftwerkzeuge benötigt.

Es gibt drei Schriftfamilien, eine pro Themenfamilie:

| Familie | Schriftfamilie | Thema | Lizenz |
| --- | --- | --- | --- |
| `ui` | Barlow Condensed | Material, Classic | OFL 1.1 |
| `hud` | Rajdhani | Reticle | OFL 1.1 |
| `lcars` | LCARS GTJ3 | LCARS | Freeware, © [GTJLCARS.de](https://www.gtjlcars.de) |

Die `ui`-Schrift ist schmal, weil eine Kachel schmal ist. Mit normaler Barlow passten zu viele Zustandszeilen nicht und wurden abgeschnitten. Barlow Condensed hat dieselben Buchstabenformen und dieselbe Versalhöhe und setzt Text etwa 25 % schmaler.

LCARS GTJ3 stammt von Jens Goettling, [www.gtjlcars.de](https://www.gtjlcars.de). Die Schrift ist Freeware: Sie darf überall verwendet werden, aber die Schriftdateien dürfen nicht umbenannt, verändert oder verkauft werden. Deshalb liegt die TTF nicht im Repository, nur die erzeugten Glyphen-Teilmengen. LCARS GTJ3 hat nur Großbuchstaben und nur eine Stärke. Alle drei LCARS-Größen verwenden diese Stärke. Bei gleicher Pixelgröße sind die Großbuchstaben niedriger als bei Antonio. Deshalb sind die LCARS-Größen 19, 26 und 43 px statt 16, 22 und 36 px. Damit haben die Großbuchstaben dieselbe Höhe wie bei Antonio (14, 19 und 31 px), und Text wirkt so groß wie vorher. Er ist etwa 12 % schmaler.

Bei `ui` und `hud` wird jede der drei Größen wird aus einem **anderen statischen Schriftschnitt** ihrer Familie erzeugt: Regular bei 16, Medium bei 22, SemiBold bei 36. Eine Bildunterschrift, eine Beschriftung und ein Messwert unterscheiden sich sowohl in der Stärke als auch in der Größe. Daraus entsteht die typografische Hierarchie.

Das ist auch der Grund für Barlow und Rajdhani. Inter, Manrope, Figtree, Outfit, Public Sans, Work Sans, DM Sans, IBM Plex Sans, Archivo, Saira und Space Grotesk gibt es in `google/fonts` nur als variable Schriften. `lv_font_conv` rendert bei einer variablen Schrift nur die Standardinstanz.

Die neun Schriftdateien belegen etwa 200 KB Flash-Speicher. Damit sind sie nach LVGL selbst der größte einzelne Bestandteil der Firmware.

> **HINWEIS:** Die ursprüngliche UI verwendete Roboto (Apache-2.0). Roboto ist nicht mehr in der Firmware enthalten. Das Classic-Thema verwendet Barlow Condensed, wie auch Material. Die Uhr des Bildschirmschoners behält die normale Barlow SemiBold.

## Neu generieren

Generiere die Schriftdateien neu, wenn du eine Schriftfamilie, eine Größe oder einen Glyphenbereich änderst:

```bash
LCARS_FONT=~/Downloads/LCARSGTJ3.zip tools/build_fonts.sh
```

Das Skript benötigt `lv_font_conv` (ein npm-Werkzeug) und Netzwerkzugriff, um die Schriftdateien von Google Fonts abzurufen. Die Familie `lcars` benötigt zusätzlich `LCARS_FONT`: den Pfad zu `LCARSGTJ3.ttf` oder zu `LCARSGTJ3.zip` von [www.gtjlcars.de](https://www.gtjlcars.de/LCARSindex/LCARSFONTS.htm). Um nur eine Familie neu zu generieren, nenne sie: `tools/build_fonts.sh ui`.
