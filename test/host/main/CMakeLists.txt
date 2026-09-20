# Four files are compiled straight out of main/ rather than mocked, and they
# are the ones that touch neither LVGL nor the network: the settings table, the
# beacon parsers, the sitemap model and parser, and the fixture pages it is fed
# here. That is not a coincidence -- port_ble.h yields raw advertisement bytes
# and leaves the parsing above the port layer precisely so that it can be
# reached from here.
#
# openhab_connector.cpp joined them when the sitemap fetch was split from the
# parse: it used to include openhab_http.hpp and the simulator fixtures, and
# now includes ArduinoJson and libc and nothing else. Two hundred lines that
# decide what every tile on the panel is had never been covered, because
# covering them would have meant a network.
#
# peripherals/relay.cpp and peripherals/led.cpp joined them for the same
# reason: the pin is behind port_relay.h and port_led.h, the broker is behind
# ohez_mqtt.hpp, and what is left is the part that decides what a payload
# means. test_outputs.cpp supplies both edges as doubles. They are also the
# only two modules with no other front end at all -- no widget, no setting --
# so a test is the only place their behaviour is written down twice.
#
# net/mdns_query.c is here for the reason the multipart scanner below is, only
# more so: it reads a packet a stranger on the network composed, on the task
# that draws, and the shapes that matter are the ones no openHAB ever sends --
# a name that never terminates, a compression pointer that points at itself or
# forwards, a record whose length runs past the end. None of those can be
# produced by asking a real server nicely, and each of them would hang or
# misread the panel rather than fail to find a host. What is in the file is
# libc and arithmetic; the socket is above it in openhab_discover.cpp.
#
# web/multipart.c joined them when the firmware upload's scanner was split out
# of webui_ota.cpp. That file called esp_ota_begin() and esp_ota_write()
# directly, which made all of it device-only, so the one path in this firmware
# that can leave a panel unbootable -- and the only one that parses bytes a
# stranger chose, with no authentication in front of it -- had no test at all.
# The scanner writes into a sink now; test_multipart.cpp supplies a buffer.
#
# config.cpp joined them when loadConfig() and saveConfig() stopped naming
# settings: what they do now is read the JSON path and the default off each row
# of config_fields[], which makes the *file format* a property of that table
# and worth pinning down. It brings the two halves of port_storage with it --
# plain stdio over a directory $OHEZ_CONFIG_DIR names, so the tests write real
# files without needing SPIFFS or a device.
#
# testif/testif_parse.c joined them for the reason multipart.c did, and it is
# the only file of main/testif/ that could: the rest of the simulator's control
# interface is a socket, an LVGL input device and a dump of the screen, and none
# of those exists here. What is left is the tokeniser -- which reads a datagram
# somebody outside the process composed, and decides where each argument of a
# command that presses buttons begins and ends.
#
# control/beeper_common.c, control/beeper_mixer.c and control/beeper_seq.c joined
# them when the beeper went polyphonic, and the split is the same one
# ui_geometry.hpp already had. control/beeper_song.c came later and for the same
# reason its header gives: it is a table of eighty notes and nothing else, the
# thing that plays it is a task, and the only claim anyone can check without a
# buzzer is that the notes obey the engine's rules -- which is exactly what
# test_ui_beep_tunes.cpp now does at its foot. Both engines are here because both ship: one is
# selected at build time and the other is one menuconfig entry away. beeper_control.cpp is a
# queue, a task and vTaskDelay(), none of which can be reached here; what came
# out of it includes nothing but <stdint.h>, and is the part that decides what
# every chime on this panel sounds like. It is also the only place any of that
# is checked at all -- the host target is silent, the Lanbon has no buzzer, and
# the ArduiTouch that does has never had this firmware on it.
#
# icons/icon_set.cpp joined them because it is the one file of the built-in
# icon set that decides anything: the rest is a generated table. It resolves a
# widget's icon *before* the network does, so once the set is compiled in its
# rules are the ones the panel obeys and openHAB no longer gets a say -- and a
# rule implemented wrongly there does not fail, it quietly draws a different
# picture than the server would have. It includes nothing but libc.
#
# It cannot be tested against the real table: tools/build_icon_set.py generates
# that from artwork this repository is not allowed to carry, so a fresh clone
# has none. ICON_SET_DATA_HEADER below points the file at icon_set_fixture.h
# instead, which is ten entries shaped like the generated ones.
#
# ui/ui_frame_stats.c joined them the day the frame started being measured, and
# the split that let it is the same one beeper_control.cpp got: the event
# callbacks that time a frame need a display and live in ui_frame_probe.c, and
# what came out is sums, a window and a mean, including nothing but libc. The
# two decisions worth a test are not the averaging -- an empty window is
# discarded rather than published, so an untouched panel keeps the last frame
# rate it really had, and every comparison is on an unsigned difference so the
# 32-bit millisecond clock can roll over inside a window. Neither is visible
# from anywhere else: what the status page shows is one struct, and a wrong one
# looks exactly like a slow panel.
#
# Everything else in main/ still is not reachable from here, deliberately.
idf_component_register(SRCS "test_runner.cpp"
                            "test_beeper_mixer.cpp"
                            "test_beeper_seq.cpp"
                            "test_ble_beacon.cpp"
                            "test_config_fields.cpp"
                            "test_config_file.cpp"
                            "test_frame_stats.cpp"
                            "test_icon_set.cpp"
                            "test_item_setters.cpp"
                            "test_mdns_query.cpp"
                            "test_multipart.cpp"
                            "test_item_state.cpp"
                            "test_item_urls.cpp"
                            "test_outputs.cpp"
                            "test_sitemap_list.cpp"
                            "test_sitemap_parse.cpp"
                            "test_ui_beep_chimes.cpp"
                            "test_ui_beep_tunes.cpp"
                            "test_ui_geometry.cpp"
                            "test_ui_theme.cpp"
                            "test_testif_parse.cpp"
                            "test_webui_api.cpp"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/control/beeper_common.c"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/control/beeper_mixer.c"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/control/beeper_seq.c"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/control/beeper_song.c"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/ui/ui_beep_tables.cpp"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/ui/ui_beep_tables_seq.cpp"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/ble/ble_beacon.cpp"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/config/config.cpp"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/icons/icon_set.cpp"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/config/config_fields.cpp"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/port/port_storage_stdio.c"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/port/linux/port_storage.c"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/openhab/openhab_connector.cpp"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/peripherals/led.cpp"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/net/mdns_query.c"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/web/multipart.c"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/web/webui_api_json.cpp"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/testif/testif_parse.c"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/ui/ui_frame_stats.c"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/peripherals/relay.cpp"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/sim/sitemap_fixture.cpp"
                       INCLUDE_DIRS "."
                                    "${CMAKE_CURRENT_LIST_DIR}/../../../main"
                                    "${CMAKE_CURRENT_LIST_DIR}/../../../main/port"
                       REQUIRES unity ArduinoJson freertos)

# icon_set.cpp reads ICON_SET_DATA_HEADER instead of looking for the generated
# table; see the note above and icon_set_fixture.h. Quoted so the preprocessor
# sees a header name, and scoped to the one file that cares.
#
# CONFIG_OHEZ_ICONS_BUILTIN comes with it because this is a different project
# from the firmware and does not read main/Kconfig.projbuild, so its sdkconfig.h
# has never heard of the option -- and the file reads an absent one as "off",
# which would compile the empty stub and leave every lookup test asserting
# against nothing at all.
set_source_files_properties(
    "${CMAKE_CURRENT_LIST_DIR}/../../../main/icons/icon_set.cpp"
    TARGET_DIRECTORY ${COMPONENT_LIB}
    PROPERTIES COMPILE_DEFINITIONS
               "CONFIG_OHEZ_ICONS_BUILTIN=1;ICON_SET_DATA_HEADER=\"icon_set_fixture.h\""
)

# Both beeper engines' tables, in one binary, each compiled as though its own
# engine had been selected.
#
# The same reason as above -- this project does not read main/Kconfig.projbuild,
# so nothing here has heard of CONFIG_OHEZ_BEEPER_ENGINE and every guarded file
# would take the #error branch. Forced per file rather than for the whole
# component, because the point is that BOTH sets are checked in one run:
# fifty-one hand-written sounds each, on a target that is silent, so a missing
# one is invisible until somebody flashes a panel. The two sets use different
# type and symbol names precisely so they can be linked side by side -- see the
# note in ui_beep.hpp.
set_source_files_properties(
    "${CMAKE_CURRENT_LIST_DIR}/../../../main/ui/ui_beep_tables.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test_ui_beep_chimes.cpp"
    TARGET_DIRECTORY ${COMPONENT_LIB}
    PROPERTIES COMPILE_DEFINITIONS "CONFIG_OHEZ_BEEPER_ENGINE_MIXER=1")

set_source_files_properties(
    "${CMAKE_CURRENT_LIST_DIR}/../../../main/ui/ui_beep_tables_seq.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../../main/control/beeper_song.c"
    "${CMAKE_CURRENT_LIST_DIR}/test_ui_beep_tunes.cpp"
    TARGET_DIRECTORY ${COMPONENT_LIB}
    PROPERTIES COMPILE_DEFINITIONS "CONFIG_OHEZ_BEEPER_ENGINE_SEQ=1")

# webui_api_json.cpp reaches the sound vocabulary through ui_beep.hpp, which
# wants an engine selected; the vocabulary is the same under both, so either
# define serves -- the same trick as the table files above.
set_source_files_properties(
    "${CMAKE_CURRENT_LIST_DIR}/../../../main/web/webui_api_json.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test_webui_api.cpp"
    TARGET_DIRECTORY ${COMPONENT_LIB}
    PROPERTIES COMPILE_DEFINITIONS "CONFIG_OHEZ_BEEPER_ENGINE_MIXER=1")
