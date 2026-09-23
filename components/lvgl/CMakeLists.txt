# LVGL as a project-owned component.
#
# LVGL ships its own ESP-IDF glue in env_support/cmake/esp.cmake, but that file
# does `PRIV_REQUIRES esp_timer log` and hard-codes the FreeRTOS include path of
# an Xtensa build, so it cannot be used for the `linux` target. Registering the
# sources ourselves is both shorter and target-agnostic.
cmake_minimum_required(VERSION 3.22.1)

set(LVGL_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/lvgl")
if(NOT EXISTS "${LVGL_ROOT_DIR}/lvgl.h")
    message(FATAL_ERROR
        "components/lvgl/lvgl is empty. Run: git submodule update --init --recursive")
endif()

file(GLOB_RECURSE LVGL_SOURCES "${LVGL_ROOT_DIR}/src/*.c" "${LVGL_ROOT_DIR}/src/*.cpp")

# The SDL display and mouse drivers are the simulator's backend; lv_conf.h
# switches LV_USE_SDL on for CONFIG_IDF_TARGET_LINUX only.
if(NOT CONFIG_IDF_TARGET_LINUX)
    list(FILTER LVGL_SOURCES EXCLUDE REGEX "/src/drivers/sdl/")
endif()
# Backends for platforms this project does not target.
list(FILTER LVGL_SOURCES EXCLUDE REGEX
     "/src/drivers/(x11|wayland|windows|uefi|nuttx|qnx|evdev|libinput|opengles)/")
# ThorVG is a large C++ vector renderer; LV_USE_THORVG is off in lv_conf.h.
list(FILTER LVGL_SOURCES EXCLUDE REGEX "/src/libs/thorvg/")

# The project's generated fonts belong to THIS component, not to the app:
# lv_conf.h points LV_FONT_DEFAULT at custom_font_roboto_16, so LVGL's own core
# (lv_style.c, lv_draw_label.c, lv_theme_simple.c) references the symbol. With
# the fonts in a component that merely *depends on* lvgl, the linker searches
# that archive first and the symbol goes unresolved.
# CONFIGURE_DEPENDS because the sources are generated: tools/build_fonts.sh
# rewrites them and can add a new one, and a plain GLOB would only notice at the
# next reconfigure -- a font that exists on disk but not in the build is an
# undefined reference at link time.
#
# Not in script mode, though: IDF's component_get_requirements.cmake includes
# this file with cmake -P to collect properties, and file(GLOB
# CONFIGURE_DEPENDS) is not allowed there. The sources are ignored in that pass
# anyway, so the plain form costs nothing.
if(CMAKE_SCRIPT_MODE_FILE)
    file(GLOB OHEZ_FONT_SOURCES "${CMAKE_CURRENT_LIST_DIR}/fonts/*.c")
else()
    file(GLOB OHEZ_FONT_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/fonts/*.c")
endif()

# REQUIRES is empty on purpose. In particular do NOT require esp_timer: it
# registers headers-only on the linux target, so requiring it fails there.
idf_component_register(
    SRCS ${LVGL_SOURCES} ${OHEZ_FONT_SOURCES}
    INCLUDE_DIRS "${LVGL_ROOT_DIR}" "${LVGL_ROOT_DIR}/src" "${CMAKE_CURRENT_LIST_DIR}"
    REQUIRES "")

# lv_conf.h sits next to this file and is found through the include dir above.
target_compile_definitions(${COMPONENT_LIB} PUBLIC "LV_CONF_INCLUDE_SIMPLE=1")

if(CONFIG_IDF_TARGET_LINUX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    target_link_libraries(${COMPONENT_LIB} PUBLIC PkgConfig::SDL2)
endif()

# Upstream code, built with this project's warning flags; do not police it.
#
# Split by language because -Wno-error=discarded-qualifiers is a C-only option
# and gcc complains when it is handed to g++ -- which, under IDF v6's -Werror,
# is itself an error. Twenty of the globbed sources are C++ (the gltf and
# thorvg-adjacent ones, and the two Arduino fsdrv files), so the flag cannot go
# on the whole list.
#
# What needs it: lv_strchr() and its neighbours in src/stdlib/clib/lv_string_clib.c
# return strchr()'s char * from a const char * argument. IDF v6.0 changed
# CONFIG_COMPILER_DISABLE_DEFAULT_ERRORS to default n, which swaps -Werror=all
# for plain -Werror; discarded-qualifiers is outside the -Wall group, so what was
# a warning up to v5.4 fails the build from v6.0 on. Kept as a warning rather
# than silenced, so it still shows up when LVGL is next updated.
set(LVGL_WARNING_OPTIONS "-Wno-unused-parameter;-Wno-sign-compare;-Wno-format")

set(LVGL_C_SOURCES ${LVGL_SOURCES})
list(FILTER LVGL_C_SOURCES INCLUDE REGEX "\\.c$")
set(LVGL_CXX_SOURCES ${LVGL_SOURCES})
list(FILTER LVGL_CXX_SOURCES INCLUDE REGEX "\\.cpp$")

set_source_files_properties(${LVGL_C_SOURCES} PROPERTIES
    COMPILE_OPTIONS "${LVGL_WARNING_OPTIONS};-Wno-error=discarded-qualifiers")
set_source_files_properties(${LVGL_CXX_SOURCES} PROPERTIES
    COMPILE_OPTIONS "${LVGL_WARNING_OPTIONS}")

# -O2, against the project's -Os.
#
# There is no 2D accelerator on an ESP32, so every pixel on the panel is written
# by one of the loops in src/draw/sw/ -- and -Os is a poor trade for exactly
# those: it refuses to unroll the 32-bit-at-a-time fill loops, keeps the blend
# helpers out of line, and turns the per-pixel mixes back into calls. The
# saving is a fraction of a 4 MB flash; the cost is frame time on the one task
# that has none to spare.
#
# Gated on the project actually asking for size rather than applied
# unconditionally: a build switched to -Og or -O0 to debug something wants LVGL
# to follow it there, not to stay optimised behind its back. That also leaves
# the simulator (CONFIG_COMPILER_OPTIMIZATION_DEBUG, sdkconfig.defaults.linux)
# alone, which is where LVGL is stepped through.
#
# IDF puts its own -Os on the command line first and this appends, so the later
# -O2 is what GCC takes.
if(CONFIG_COMPILER_OPTIMIZATION_SIZE)
    target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
endif()
