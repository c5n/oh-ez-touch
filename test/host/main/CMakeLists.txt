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
# Everything else in main/ still is not reachable from here, deliberately.
idf_component_register(SRCS "test_runner.cpp"
                            "test_ble_beacon.cpp"
                            "test_config_fields.cpp"
                            "test_config_file.cpp"
                            "test_item_setters.cpp"
                            "test_multipart.cpp"
                            "test_item_state.cpp"
                            "test_item_urls.cpp"
                            "test_outputs.cpp"
                            "test_sitemap_parse.cpp"
                            "test_ui_geometry.cpp"
                            "test_ui_theme.cpp"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/ble/ble_beacon.cpp"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/config/config.cpp"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/config/config_fields.cpp"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/port/port_storage_stdio.c"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/port/linux/port_storage.c"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/openhab/openhab_connector.cpp"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/peripherals/led.cpp"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/web/multipart.c"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/peripherals/relay.cpp"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/sim/sitemap_fixture.cpp"
                       INCLUDE_DIRS "."
                                    "${CMAKE_CURRENT_LIST_DIR}/../../../main"
                                    "${CMAKE_CURRENT_LIST_DIR}/../../../main/port"
                       REQUIRES unity ArduinoJson freertos)
