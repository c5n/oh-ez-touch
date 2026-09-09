# ESP-IDF project definition for oh-ez-touch.
#
# Build for a device -- once per board, and the board's defaults file is what
# selects it:
#   idf.py -B build/arduitouch -DSDKCONFIG=build/arduitouch/sdkconfig \
#          -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32;sdkconfig.defaults.arduitouch" \
#          set-target esp32
#   idf.py -B build/arduitouch build flash monitor
#
# sdkconfig.defaults.arduitouch28 and sdkconfig.defaults.lanbon are the other
# two, and sdkconfig.defaults.arduitouch_jtag is the 2.4" with the pins an
# attached esp-prog needs moved. Omitting the board file gives the ArduiTouch
# 2.4", which is the Kconfig default.
#
# Build the host simulator:
#   idf.py -B build/linux -DSDKCONFIG=build/linux/sdkconfig \
#          -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.linux" \
#          --preview set-target linux
#   idf.py -B build/linux build
#
# -DSDKCONFIG is not optional when both targets are in play: idf.py otherwise
# puts the generated sdkconfig at the project root, where the two targets
# overwrite each other and the second build refuses to start. Keeping it inside
# the build directory means -B alone identifies a build.
#
# A defaults file only seeds a *new* sdkconfig, so after editing one, delete
# that build's sdkconfig (or the whole build directory) and re-run set-target.
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

# What the boot banner, the Info tab and the web status page call this build.
# Was -DTARGET_NAME per platformio.ini environment, and is the board choice from
# main/Kconfig.projbuild now. The names are the environments' own, so an
# existing installation reports the same string it did before.
if(CONFIG_IDF_TARGET_LINUX)
    set(OHEZ_TARGET_NAME "Simulator")
elseif(CONFIG_OHEZ_BOARD_ARDUITOUCH28)
    set(OHEZ_TARGET_NAME "ArduiTouch28")
elseif(CONFIG_OHEZ_BOARD_LANBON)
    set(OHEZ_TARGET_NAME "Lanbon")
elseif(CONFIG_OHEZ_ARDUITOUCH_JTAG_PINS)
    set(OHEZ_TARGET_NAME "ArduiTouchJTAG")
else()
    set(OHEZ_TARGET_NAME "ArduiTouch")
endif()
idf_build_set_property(COMPILE_DEFINITIONS "TARGET_NAME=\"${OHEZ_TARGET_NAME}\"" APPEND)

# data/ becomes the `spiffs` partition. FLASH_IN_PROJECT builds the image during
# a normal build and flashes it with the firmware, so PlatformIO's separate
# `pio run -t uploadfs` step disappears. There is no SPIFFS on the linux target
# (esp_spiffs.c is excluded there), where config.json is a plain host file.
if(NOT CONFIG_IDF_TARGET_LINUX)
    spiffs_create_partition_image(spiffs data FLASH_IN_PROJECT)
endif()
