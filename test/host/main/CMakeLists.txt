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
# Everything else in main/ still is not reachable from here, deliberately.
idf_component_register(SRCS "test_runner.cpp"
                            "test_ble_beacon.cpp"
                            "test_config_fields.cpp"
                            "test_item_setters.cpp"
                            "test_item_state.cpp"
                            "test_item_urls.cpp"
                            "test_sitemap_parse.cpp"
                            "test_ui_theme.cpp"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/ble/ble_beacon.cpp"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/config/config_fields.cpp"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/openhab/openhab_connector.cpp"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/sim/sitemap_fixture.cpp"
                       INCLUDE_DIRS "."
                                    "${CMAKE_CURRENT_LIST_DIR}/../../../main"
                       REQUIRES unity ArduinoJson freertos)
