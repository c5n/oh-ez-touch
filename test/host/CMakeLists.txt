# The host test app.
#
# An ESP-IDF project of its own on the linux target, replacing `pio test -e
# linux`. It is separate from the firmware project rather than a component of
# it because it has a different app_main(): one that runs the suites and exits
# with the number of failures, so a shell or a CI job can tell.
#
# Run with:
#   cd test/host
#   idf.py --preview set-target linux
#   idf.py build && ./build/oh-ez-touch-host-test.elf
#
# Both suites cover header-only code -- the inline setters in
# openhab_connector.hpp and the name lookups in ui_theme.hpp -- so nothing from
# main/ has to be linked, which is what keeps this app small enough to be worth
# having.
cmake_minimum_required(VERSION 3.22.1)

# ArduinoJson, which openhab_connector.hpp includes. The rest of the firmware's
# components are deliberately not reachable: a test that needed LVGL would be a
# different kind of test.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components/ArduinoJson")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Expand requirements from main only, instead of from every component in the
# IDF tree. This is what IDF's own host tests do (components/*/host_test), and
# from IDF v6.1 it is not optional: with the full set, requirement expansion
# reaches esp_driver_ana_cmpr, which requires esp_hal_ana_cmpr unconditionally
# while esp_hal_ana_cmpr's own CMakeLists returns early on the linux target --
# so the configure step fails with "component not registered" on a component
# this app never uses. Trimming to main sidesteps it and configures faster.
set(COMPONENTS main)

project(oh-ez-touch-host-test)
