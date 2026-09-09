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
        "port/linux/port_backlight.c"
        "port/linux/port_beeper.c"
        "port/linux/port_bme280.c"
        "port/linux/port_net.c"
        "port/linux/port_ntp.c"
        "port/linux/port_esp_timer.c")
else()
    set(OHEZ_PORT_SRCS
        "port/esp32/port_sys.c"
        "port/esp32/port_storage.c"
        "port/esp32/port_flash.c"
        "port/esp32/port_display.c"
        "port/esp32/port_indev.c"
        "port/esp32/port_backlight.c"
        "port/esp32/port_beeper.c"
        "port/esp32/port_bme280.c"
        "port/esp32/port_net.c"
        "port/esp32/port_ntp.c"
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
    "control/beeper_control.cpp")

# One directory per concern. Only main.cpp stays at the top; config/ holds the
# settings and the one field table that both the panel's settings screen and the
# web form walk, which is what the rest of them have in common.
set(OHEZ_APP_SRCS
    "main.cpp"

    "config/config.cpp"
    "config/config_fields.cpp"

    "ui/openhab_ui.cpp"
    "ui/ui_settings.cpp"
    "ui/ui_style.cpp"

    "openhab/openhab_connector.cpp"
    "openhab/openhab_http.cpp"
    "openhab/openhab_sensor_bme280.cpp"
    "openhab/openhab_sensor_connector.cpp"
    "openhab/openhab_sensor_main.cpp"

    "mqtt/ohez_mqtt.cpp"

    "web/webui.cpp"
    "web/webui_ota.cpp"
    "web/webui_transport.cpp"
    "web/webui_transport_esp32.cpp"
    "web/webui_transport_linux.cpp"

    "net/wlan.cpp"
    "net/wlan_radio.cpp"

    "sim/icon_fixture.cpp"
    "sim/sim_offline.cpp"
    "sim/sitemap_fixture.cpp")

set(OHEZ_SRCS ${OHEZ_APP_SRCS})

# mqtt is in both lists: components/mqtt builds for the linux target as well,
# and every socket wait in its transport goes through select(), which is the one
# thing the FreeRTOS simulator wraps -- so the panel and the simulator run the
# same MQTT client, the way they already run the same esp_http_client. The one
# symbol it is missing there comes from port/linux/port_esp_timer.c.
set(OHEZ_REQUIRES lvgl lodepng ArduinoJson nvs_flash esp_partition esp_http_client
                  esp_event mqtt esp_timer)
if(NOT OHEZ_LINUX)
    # esp_timer is in the shared list for its *header*: it registers
    # headers-only on the linux target, so esp_timer.h is includable there but
    # nothing in it links -- which is exactly why port_sys has two
    # implementations, and why port/linux/port_esp_timer.c has to define the one
    # function esp-mqtt calls. spiffs likewise excludes esp_spiffs.c on linux.
    list(APPEND OHEZ_REQUIRES spiffs)

    # The panel, the touch panel and the two PWMs. The three registry drivers
    # come from main/idf_component.yml; only one panel and one touch driver is
    # reachable in a given build, and the linker drops the rest.
    list(APPEND OHEZ_REQUIRES
        esp_lcd
        esp_driver_spi
        esp_driver_ledc
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
    # Without this the archive order defeats it: libmqtt.a is scanned before
    # libmain.a, so nothing is taken from it on that pass; libmain.a then pulls
    # in the MQTT client, which leaves esp_mqtt_client_init undefined; and the
    # second libmqtt.a on the line satisfies that by extracting
    # platform_esp32_idf.c.o -- whose esp_timer_get_time has, by then, no
    # archive left to come from. `-u` puts the symbol on the undefined list
    # before libmain.a is scanned, so the definition is loaded with everything
    # else. The same trick IDF's own esp_timer component uses to keep
    # esp_timer_init_include_func.
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u esp_timer_get_time")

    # port/linux/port_flash.c needs the partition table the build just
    # generated, to seed the emulated flash image with. Only CMake knows where
    # the build directory is.
    idf_build_get_property(ohez_build_dir BUILD_DIR)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        "PORT_FLASH_TABLE_BIN=\"${ohez_build_dir}/partition_table/partition-table.bin\"")
endif()
