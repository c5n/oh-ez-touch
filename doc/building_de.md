# Bauen und Flashen

Dieses Projekt wird mit [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/) gebaut.
Dieselbe Toolchain erstellt die Firmware und den Desktop-Simulator.

Es sind nur Anleitungen für Linux verfügbar.

## Voraussetzungen

Installieren Sie ESP-IDF **v6.1**. Die Version ist fest vorgegeben. Das vom
Simulator verwendete Ziel `linux` ist eine Vorschaufunktion. Ein Upgrade von
IDF muss überprüft werden.

```bash
mkdir -p ~/esp
git -C ~/esp clone -b v6.1 --depth 1 --recursive https://github.com/espressif/esp-idf.git
~/esp/esp-idf/install.sh esp32
```

Führen Sie `. ~/esp/esp-idf/export.sh` einmal pro Shell aus. Dadurch wird
`idf.py` in den PATH aufgenommen.

Installieren Sie auf Debian-/Ubuntu-basierten Distributionen zusätzlich:

```bash
sudo apt install libsdl2-dev libbsd-dev pkg-config ninja-build
```

`libsdl2-dev` stellt das Fenster des Simulators bereit. `libbsd-dev` ist
unverzichtbar. IDFs `components/linux/linux_include/string.h` bindet
`<bsd/string.h>` ein. Ohne `libbsd-dev` schlägt die Kompilierung jeder
Übersetzungseinheit des Simulators fehl; IDFs CMake gibt dazu lediglich eine
Warnung aus.

## Quellcode beziehen

Das Projekt enthält zwei Bibliotheken als Submodule. Klonen Sie rekursiv:

```bash
git clone --recurse-submodules https://github.com/c5n/oh-ez-touch.git
```

Führen Sie in einer vorhandenen Arbeitskopie `git submodule update --init --recursive` aus.

Um den Projektordner später zu aktualisieren, führen Sie `git pull --recurse-submodules` aus.

## Konfiguration vor dem Build

Die Datei `data/config.json` enthält die Standardwerte für Hostname, NTP,
Erscheinungsbild, Hintergrundbeleuchtung, Signalgeber und den openHAB-Server.
Ein neues Gerät startet mit diesen Werten. Jeder Wert kann später über die
Weboberfläche oder am Bedienfeld geändert werden. `idf.py flash` schreibt die
Datei zusammen mit der Firmware in das Geräte-Dateisystem.

Die eingebauten Standardwerte gelten für alle Werte, die in der Datei nicht
erwähnt werden. Eine teilweise ausgefüllte `config.json` ist gültig. Fehlt
die Datei, bleibt die Konfiguration vollständig und funktionsfähig. Die
Standardwerte stehen in `main/config/config_fields.cpp`. Dort hat jede
Einstellung einen Wertebereich. Werte außerhalb dieses Bereichs werden
begrenzt. Ein Hostname mit `/` oder `:` wird abgelehnt und der Standardwert
beibehalten.

WLAN-Zugangsdaten stehen nicht in dieser Datei. Sie werden im NVS des ESP32
gespeichert. NVS bleibt bei einem OTA-Update und beim erneuten Flashen des
Dateisystems erhalten. Von Firmware vor Version 0.90 gespeicherte Zugangsdaten
(AutoConnect) werden beim ersten Start migriert.

Ein Gerät ohne Zugangsdaten öffnet einen Access Point mit seinem Hostnamen.
Auf dem Bildschirm werden der Name und die Adresse angezeigt. Siehe
[Konfiguration](configuration_de.md#wlan-einrichten).

## Bauen

Für jedes Board wird ein eigener Build erstellt. Eine Defaults-Datei wählt das
Board aus. Anhand des Build-Verzeichnisses lässt sich der Build später
identifizieren, auch mit dem Update-Tool.

| Board | Defaults-Datei | Build-Verzeichnis |
| --- | --- | --- |
| [ArduiTouch 2.4"](hardware/arduitouch_de.md) | `sdkconfig.defaults.arduitouch` | `build/arduitouch` |
| [ArduiTouch 2.8"](hardware/arduitouch28_de.md) | `sdkconfig.defaults.arduitouch28` | `build/arduitouch28` |
| [Lanbon L8](hardware/lanbon_de.md) | `sdkconfig.defaults.lanbon` | `build/lanbon` |
| [Cheap Yellow Display](hardware/cyd_de.md) | `sdkconfig.defaults.cyd` | `build/cyd` |
| ArduiTouch 2.4" mit JTAG-Pins | `sdkconfig.defaults.arduitouch_jtag` | `build/arduitouch_jtag` |

Beispiel für das ArduiTouch 2.4":

```bash
cd oh-ez-touch
. ~/esp/esp-idf/export.sh

idf.py -B build/arduitouch -DSDKCONFIG=build/arduitouch/sdkconfig \
       -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32;sdkconfig.defaults.arduitouch" \
       set-target esp32
idf.py -B build/arduitouch build
```

Ohne eine Board-Defaults-Datei gilt der Kconfig-Standardwert: ArduiTouch
2.4".

`-DSDKCONFIG` ist erforderlich, wenn Sie mehr als ein Ziel bauen. Ohne diese
Angabe schreibt `idf.py` die generierte `sdkconfig` in das Projektverzeichnis.
Die Builds würden sich gegenseitig die Datei überschreiben. Liegt die Datei
im Build-Verzeichnis, identifiziert `-B` allein einen Build.

Eine Defaults-Datei liefert nur die Ausgangswerte für eine **neue**
`sdkconfig`. Löschen Sie nach einer Änderung an einer Defaults-Datei die
`sdkconfig` des Builds (oder das gesamte Build-Verzeichnis) und führen Sie
`set-target` erneut aus.

Mit `idf.py -B build/arduitouch menuconfig` erhalten Sie Zugriff auf alle
weiteren Einstellungen: die Board-Auswahl, die Signalgeber-Engine, die
JTAG-Pin-Neuzuordnung und die Debug-Ausgabe der einzelnen Module unter
**OhEzTouch**.

Um alle Boards auf einmal zu bauen, verwenden Sie `tools/build_release.py`.
Siehe [Update-Tool](update_de.md#release-builds).

## Flashen

Ein Befehl lädt den Bootloader, die Partitionstabelle, die Firmware und das aus
`data/` erstellte Dateisystem-Image hoch:

```bash
idf.py -B build/arduitouch -p /dev/ttyUSB0 flash monitor
```

Die ArduiTouch-Boards haben keinen USB-Anschluss. Sie benötigen einen
UART-Adapter. Siehe [ArduiTouch 2.4 Zoll](hardware/arduitouch_de.md#flashen-mit-einem-uart-adapter).

## Simulator

Die Benutzeroberfläche kann auch ohne Hardware auf dem Entwicklungsrechner
ausgeführt werden. Siehe [Simulator](simulator_de.md).

## Schriftarten

Die LVGL-Schriftquellen werden generiert und eingecheckt. Für einen normalen
Build sind keine Werkzeuge für Schriftarten erforderlich. Siehe
[Schriftarten](fonts_de.md) zur erneuten Generierung.
