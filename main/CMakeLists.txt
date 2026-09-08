# The application component.
#
# The application sources moved in here from src/ but are not built yet: they
# are still written against the Arduino framework, which no longer exists on
# either target. They join SRCS module by module as the port layer grows under
# them; selftest.cpp holds the app_main() that keeps both targets linking and
# exercises what is already ported, and goes away with the last of them.

# The platform boundary: one implementation directory per target, selected here.
# This is what replaces platformio.ini's `build_src_filter` subtractions -- a
# concern a target cannot provide is absent from its directory, so leaving one
# out is a link error rather than a silent no-op.
if(CONFIG_IDF_TARGET_LINUX)
    set(OHEZ_PORT_SRCS
        "port/linux/port_sys.c"
        "port/linux/port_storage.c"
        "port/linux/port_flash.c")
else()
    set(OHEZ_PORT_SRCS
        "port/esp32/port_sys.c"
        "port/esp32/port_storage.c"
        "port/esp32/port_flash.c")
endif()

set(OHEZ_REQUIRES lvgl lodepng ArduinoJson nvs_flash esp_partition)
if(NOT CONFIG_IDF_TARGET_LINUX)
    # esp_timer is deliberately absent from the linux side: it registers
    # headers-only there, so esp_timer.h is not even includable -- which is
    # exactly why port_sys has two implementations. spiffs likewise excludes
    # esp_spiffs.c on linux.
    list(APPEND OHEZ_REQUIRES spiffs esp_timer)
endif()

idf_component_register(SRCS "selftest.cpp"
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
