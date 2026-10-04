# Schriftarten

Die LVGL-Schriftquellen in `components/lvgl/fonts/` werden generiert und eingecheckt. Für einen normalen Build werden keine Schriftwerkzeuge benötigt.

Es gibt drei Schriftfamilien, eine pro Themenfamilie:

| Familie | Schriftfamilie | Thema | Lizenz |
| --- | --- | --- | --- |
| `ui` | Barlow | Material | OFL 1.1 |
| `hud` | Rajdhani | Reticle | OFL 1.1 |
| `lcars` | Antonio | LCARS | OFL 1.1 |

Jede der drei Größen wird aus einem **anderen statischen Schriftschnitt** ihrer Familie erzeugt: Regular bei 16, Medium bei 22, SemiBold bei 36. Eine Bildunterschrift, eine Beschriftung und ein Messwert unterscheiden sich sowohl in der Stärke als auch in der Größe. Daraus entsteht die typografische Hierarchie.

Das ist auch der Grund für Barlow und Rajdhani. Inter, Manrope, Figtree, Outfit, Public Sans, Work Sans, DM Sans, IBM Plex Sans, Archivo, Saira und Space Grotesk gibt es in `google/fonts` nur als variable Schriften. `lv_font_conv` rendert bei einer variablen Schrift nur die Standardinstanz. Auch Antonio ist variabel, bleibt es aber: LCARS braucht ohnehin nur eine Stärke.

Die neun Schriftdateien belegen etwa 210 KB Flash-Speicher. Damit sind sie nach LVGL selbst der größte einzelne Bestandteil der Firmware.

> **HINWEIS:** Die ursprüngliche UI verwendete Roboto (Apache-2.0). Roboto ist nicht mehr in der Firmware enthalten. Das Classic-Thema verwendet Barlow, wie auch Material.

## Neu generieren

Generiere die Schriftdateien neu, wenn du eine Schriftfamilie, eine Größe oder einen Glyphenbereich änderst:

```bash
tools/build_fonts.sh
```

Das Skript benötigt `lv_font_conv` (ein npm-Werkzeug) und Netzwerkzugriff, um die Schriftdateien von Google Fonts abzurufen.
