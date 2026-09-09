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
cmake_minimum_required(VERSION 3.16)

# ArduinoJson, which openhab_connector.hpp includes. The rest of the firmware's
# components are deliberately not reachable: a test that needed LVGL would be a
# different kind of test.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components/ArduinoJson")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(oh-ez-touch-host-test)
