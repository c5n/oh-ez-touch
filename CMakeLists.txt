# ESP-IDF project definition for oh-ez-touch.
#
# Build for a device:
#   idf.py -B build/arduitouch \
#          -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32" \
#          set-target esp32 build flash monitor
#
# Build the host simulator:
#   idf.py -B build/linux \
#          -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.linux" \
#          --preview set-target linux build
#
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(oh-ez-touch)

# Replaces platformio.ini's `!echo '-DVERSION_GIT_HASH=...'` shell-out. ERROR_QUIET
# plus the empty-string fallback keeps a tarball export building.
execute_process(COMMAND git -C "${CMAKE_CURRENT_LIST_DIR}" rev-parse HEAD
                OUTPUT_VARIABLE OHEZ_GIT_HASH
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)
if(NOT OHEZ_GIT_HASH)
    set(OHEZ_GIT_HASH "unknown")
endif()
idf_build_set_property(COMPILE_DEFINITIONS "VERSION_GIT_HASH=\"${OHEZ_GIT_HASH}\"" APPEND)

# data/ becomes the `spiffs` partition. FLASH_IN_PROJECT builds the image during
# a normal build and flashes it with the firmware, so PlatformIO's separate
# `pio run -t uploadfs` step disappears. There is no SPIFFS on the linux target
# (esp_spiffs.c is excluded there), where config.json is a plain host file.
if(NOT CONFIG_IDF_TARGET_LINUX)
    spiffs_create_partition_image(spiffs data FLASH_IN_PROJECT)
endif()
