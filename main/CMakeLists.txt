# The application component.
#
# One source list for both targets. Everything above the port layer is shared:
# what differs between a panel and a desktop is in main/port/, and that is the
# whole of the difference.

# IDF_TARGET rather than CONFIG_IDF_TARGET_LINUX, and not interchangeably: the
# CONFIG_* variables are not set during the pass that reads REQUIRES, so a
# device-only requirement guarded on one of them leaks into the linux build and
# fails there with "component not registered".
idf_build_get_property(ohez_target IDF_TARGET)
if(ohez_target STREQUAL "linux")
    set(OHEZ_LINUX 1)
else()
    set(OHEZ_LINUX 0)
endif()

# The platform boundary: one implementation directory per target, selected here.
# This is what replaces platformio.ini's `build_src_filter` subtractions -- a
# concern a target cannot provide is absent from its directory, so leaving one
# out is a link error rather than a silent no-op.
if(OHEZ_LINUX)
    set(OHEZ_PORT_SRCS
        "port/linux/port_sys.c"
        "port/linux/port_storage.c"
        "port/linux/port_flash.c"
        "port/linux/port_display.c"
        "port/linux/port_indev.c"
        "port/touch_cal.c"
        "port/linux/port_backlight.c"
        "port/linux/port_beeper.c"
        "port/linux/port_bme280.c"
        "port/linux/port_outputs.c"
        "port/linux/port_net.c"
        "port/linux/port_ntp.c"
        "port/linux/port_ble.c"
        "port/linux/port_esp_timer.c")
else()
    set(OHEZ_PORT_SRCS
        "port/esp32/port_sys.c"
        "port/esp32/port_storage.c"
        "port/esp32/port_flash.c"
        "port/esp32/port_display.c"
        "port/esp32/port_indev.c"
        "port/touch_cal.c"
        "port/esp32/port_backlight.c"
        "port/esp32/port_beeper.c"
        "port/esp32/port_bme280.c"
        "port/esp32/port_relay.c"
        "port/esp32/port_led.c"
        "port/esp32/port_net.c"
        "port/esp32/port_ntp.c"
        "port/esp32/port_ble.c"
        "port/esp32/board_i2c.c")
endif()

# Policy that sits directly on the port layer and is worth running on both
# targets: when to dim the display, and a queue that plays a chime without
# blocking the screen for its duration.
#
# The directory is control/ rather than driver/ because IDF has a component of
# that name: with main/ on the include path, `#include "driver/ledc.h"` would
# look here first.
set(OHEZ_CONTROL_SRCS
    "control/backlight_control.cpp"
    "control/beeper_common.c"
    "control/beeper_control.cpp"
    "control/beeper_mixer.c"
    "control/beeper_seq.c"
    "control/beeper_song.c")

# One directory per concern. Only main.cpp stays at the top; config/ holds the
# settings and the one field table that both the panel's settings screen and the
# web form walk, which is what the rest of them have in common.
set(OHEZ_APP_SRCS
    "main.cpp"

    "config/config.cpp"
    "config/config_fields.cpp"

    "icons/icon_set.cpp"

    "ui/openhab_ui.cpp"
    "ui/ui_settings.cpp"
    "ui/ui_calibration.cpp"
    "ui/frames/frame_material.cpp"
    "ui/frames/frame_jarvis.cpp"
    "ui/frames/frame_lcars.cpp"
    "ui/frames/ui_frame_common.cpp"
    "ui/items/item_screen.cpp"
    "ui/items/item_slider.cpp"
    "ui/items/item_setpoint.cpp"
    "ui/items/item_selection.cpp"
    "ui/items/item_rollershutter.cpp"
    "ui/items/item_player.cpp"
    "ui/items/item_color.cpp"
    "ui/ui_activity.c"
    "ui/ui_beep.cpp"
    "ui/ui_frame_probe.c"
    "ui/ui_frame_stats.c"
    "ui/ui_input.c"
    "ui/ui_beep_tables.cpp"
    "ui/ui_beep_tables_seq.cpp"
    "ui/ui_messagebox.cpp"
    "ui/ui_motion.cpp"
    "ui/ui_screen.cpp"
    "ui/ui_style.cpp"
    "ui/ui_widgets.cpp"

    "openhab/openhab_client.cpp"
    "openhab/openhab_discover.cpp"
    "openhab/openhab_connector.cpp"
    "openhab/openhab_http.cpp"
    "openhab/openhab_sitemaps.cpp"

    "peripherals/sensor_bme280.cpp"
    "peripherals/sensor_main.cpp"
    "peripherals/relay.cpp"
    "peripherals/led.cpp"

    "mqtt/ohez_mqtt.cpp"

    "ble/ble_beacon.cpp"
    "ble/ble_scan.cpp"

    "web/multipart.c"
    "web/webui.cpp"
    "web/webui_api.cpp"
    "web/webui_api_json.cpp"
    "web/webui_ota.cpp"
    "web/webui_transport.cpp"
    "web/webui_transport_esp32.cpp"
    "web/webui_transport_linux.cpp"

    "net/mdns_query.c"
    "net/wlan.cpp"
    "net/wlan_radio.cpp"

    "sim/icon_fixture.cpp"
    "sim/sim_offline.cpp"
    "sim/sitemap_fixture.cpp"

    "testif/testif.cpp"
    "testif/testif_parse.c"
    "testif/testif_report.cpp"
    "testif/testif_shot.cpp"
    "testif/testif_touch.cpp")

# The minimal recovery firmware (CONFIG_OHEZ_MINIMAL): just enough to reach
# /update over WLAN -- the config (for the hostname), the radio, the
# transport and the OTA handler. Everything the panel is made of stays out,
# which is where the image size goes. In the early requirements-expansion
# pass CONFIG_* is unset, so this guard selects the full list there; that
# pass only computes the REQUIRES graph, which is deliberately the same for
# both builds.
if(CONFIG_OHEZ_MINIMAL)
    set(OHEZ_APP_SRCS
        "main_minimal.cpp"

        "config/config.cpp"
        "config/config_fields.cpp"

        "net/wlan.cpp"
        "net/wlan_radio.cpp"

        "web/multipart.c"
        "web/webui_minimal.cpp"
        "web/webui_ota.cpp"
        "web/webui_transport.cpp"
        "web/webui_transport_esp32.cpp")

    set(OHEZ_CONTROL_SRCS "")

    # port_storage stays: the hostname lives in config.json on SPIFFS, and
    # it is how the fleet addresses the device for the full-image upload
    # (main_minimal.cpp says why).
    set(OHEZ_PORT_SRCS
        "port/esp32/port_sys.c"
        "port/esp32/port_storage.c"
        "port/esp32/port_flash.c")
endif()

set(OHEZ_SRCS ${OHEZ_APP_SRCS})

# The built-in icon set is a generated file that may or may not be there, and
# icons/icon_set.cpp picks it up with __has_include. That alone would not
# survive the interesting case: the header is *created* by
# tools/build_icon_set.py after a build has already been made without it, and
# ninja has no reason to recompile a file over a header it never included.
#
# Watching the directory fixes that -- creating a file in it changes its mtime,
# CMake re-runs, and the compile happens. The status line is the other half:
# whether the icons are in the firmware is decided by a gitignored file, so a
# configure that does not say which it found leaves it to be inferred later from
# how the panel behaves.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_CURRENT_LIST_DIR}/icons")

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/icons/icon_set_data.h")
    file(SIZE "${CMAKE_CURRENT_LIST_DIR}/icons/icon_set_data.h" ohez_icon_data_size)
    message(STATUS "oh-ez-touch: built-in icon set present "
                   "(icon_set_data.h, ${ohez_icon_data_size} bytes of source)")
else()
    message(STATUS "oh-ez-touch: no built-in icon set -- icons come from openHAB "
                   "over HTTP; run tools/build_icon_set.py to compile them in")
endif()

# espressif__mqtt is in both lists: esp-mqtt builds for the linux target as well,
# and every socket wait in its transport goes through select(), which is the one
# thing the FreeRTOS simulator wraps -- so the panel and the simulator run the
# same MQTT client, the way they already run the same esp_http_client. The one
# symbol it is missing there comes from port/linux/port_esp_timer.c.
#
# The `espressif__` prefix is not decoration. IDF v6.0 removed components/mqtt
# from the tree, so esp-mqtt now arrives through main/idf_component.yml like the
# panel drivers do, and a managed component is registered under
# <namespace>__<name>. Same code and same API as the in-tree component was.
set(OHEZ_REQUIRES lvgl lodepng ArduinoJson nvs_flash esp_partition esp_http_client
                  esp_event espressif__mqtt esp_timer)
if(NOT OHEZ_LINUX)
    # esp_timer is in the shared list for its *header*: it registers
    # headers-only on the linux target, so esp_timer.h is includable there but
    # nothing in it links -- which is exactly why port_sys has two
    # implementations, and why port/linux/port_esp_timer.c has to define the one
    # function esp-mqtt calls. spiffs likewise excludes esp_spiffs.c on linux.
    list(APPEND OHEZ_REQUIRES spiffs)

    # The BSD sockets openhab_discover.cpp asks mDNS its one question over.
    # Device only, and not because the code is: on the linux target those
    # headers are the host's own and lwIP is not in the build at all.
    list(APPEND OHEZ_REQUIRES lwip)

    # The Bluetooth controller and the NimBLE host. Device only: there is no
    # controller on a desktop that port_ble could drive, so the linux side gets
    # the fixture implementation in port/linux/port_ble.c instead. The roles are
    # trimmed as far as they go in sdkconfig.defaults.esp32, which is what keeps
    # the stack down to something that fits beside LVGL.
    list(APPEND OHEZ_REQUIRES bt)

    # The panel, the touch panel and the two PWMs. The three registry drivers
    # come from main/idf_component.yml; only one panel and one touch driver is
    # reachable in a given build, and the linker drops the rest.
    list(APPEND OHEZ_REQUIRES
        esp_lcd
        esp_driver_spi
        esp_driver_ledc
        esp_driver_gpio
        esp_driver_i2c
        esp_wifi
        esp_netif
        esp_http_server
        app_update
        espressif__esp_lcd_ili9341
        espressif__esp_lcd_touch
        espressif__esp_lcd_touch_ft5x06
        atanisoft__esp_lcd_touch_xpt2046)
endif()

idf_component_register(SRCS ${OHEZ_SRCS}
                            ${OHEZ_CONTROL_SRCS}
                            "port/port_storage_stdio.c"
                            "port/port_kv.c"
                            ${OHEZ_PORT_SRCS}
                       INCLUDE_DIRS "." "port"
                       PRIV_INCLUDE_DIRS "port/esp32"
                       REQUIRES ${OHEZ_REQUIRES})

if(OHEZ_LINUX)
    # Force port/linux/port_esp_timer.c into the link.
    #
    # Without this the archive order defeats it: libespressif__mqtt.a is scanned
    # before libmain.a, so nothing is taken from it on that pass; libmain.a then
    # pulls in the MQTT client, which leaves esp_mqtt_client_init undefined; and
    # the second libespressif__mqtt.a on the line satisfies that by extracting
    # platform.c.o -- whose esp_timer_get_time has, by then, no archive left to
    # come from. `-u` puts the symbol on the undefined list before libmain.a is
    # scanned, so the definition is loaded with everything else. The same trick
    # IDF's own esp_timer component uses to keep esp_timer_init_include_func.
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u esp_timer_get_time")

    # port/linux/port_flash.c needs the partition table the build just
    # generated, to seed the emulated flash image with. Only CMake knows where
    # the build directory is.
    idf_build_get_property(ohez_build_dir BUILD_DIR)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        "PORT_FLASH_TABLE_BIN=\"${ohez_build_dir}/partition_table/partition-table.bin\"")
endif()
