# The application component.
#
# The application is ported target by target rather than module by module,
# because the two targets diverge in what they still need: the simulator's set
# is complete below and it builds and runs, while the device firmware waits for
# esp_lcd, esp_wifi and esp_http_server to replace TFT_eSPI, WiFi and
# WebServer. Until then the device target builds selftest.cpp -- which links,
# boots and exercises the port layer -- and the sources not listed here are
# still Arduino code that compiles on neither target.

# The platform boundary: one implementation directory per target, selected here.
# This is what replaces platformio.ini's `build_src_filter` subtractions -- a
# concern a target cannot provide is absent from its directory, so leaving one
# out is a link error rather than a silent no-op.
if(CONFIG_IDF_TARGET_LINUX)
    set(OHEZ_PORT_SRCS
        "port/linux/port_sys.c"
        "port/linux/port_storage.c"
        "port/linux/port_flash.c"
        "port/linux/port_display.c"
        "port/linux/port_indev.c")
else()
    set(OHEZ_PORT_SRCS
        "port/esp32/port_sys.c"
        "port/esp32/port_storage.c"
        "port/esp32/port_flash.c")
endif()

# The UI, the openHAB client and the settings. Target-independent code, but it
# only compiles where every port it needs exists, which today is the host.
set(OHEZ_APP_SRCS
    "main.cpp"
    "config.cpp"
    "openhab_connector.cpp"
    "openhab_ui.cpp"
    "settings_fields.cpp"
    "ui_settings.cpp"
    "ui_style.cpp"
    "sim/icon_fixture.cpp"
    "sim/sitemap_fixture.cpp")

if(CONFIG_IDF_TARGET_LINUX)
    set(OHEZ_SRCS ${OHEZ_APP_SRCS})
else()
    set(OHEZ_SRCS "selftest.cpp")
endif()

set(OHEZ_REQUIRES lvgl lodepng ArduinoJson nvs_flash esp_partition)
if(NOT CONFIG_IDF_TARGET_LINUX)
    # esp_timer is deliberately absent from the linux side: it registers
    # headers-only there, so esp_timer.h is not even includable -- which is
    # exactly why port_sys has two implementations. spiffs likewise excludes
    # esp_spiffs.c on linux.
    list(APPEND OHEZ_REQUIRES spiffs esp_timer)
endif()

idf_component_register(SRCS ${OHEZ_SRCS}
                            "port/port_storage_stdio.c"
                            "port/port_kv.c"
                            ${OHEZ_PORT_SRCS}
                       INCLUDE_DIRS "." "port"
                       REQUIRES ${OHEZ_REQUIRES})

if(CONFIG_IDF_TARGET_LINUX)
    # port/linux/port_flash.c needs the partition table the build just
    # generated, to seed the emulated flash image with. Only CMake knows where
    # the build directory is.
    idf_build_get_property(ohez_build_dir BUILD_DIR)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        "PORT_FLASH_TABLE_BIN=\"${ohez_build_dir}/partition_table/partition-table.bin\"")
endif()
