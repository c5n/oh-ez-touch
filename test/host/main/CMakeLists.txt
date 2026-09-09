# Two files are compiled straight out of main/ rather than mocked, and they are
# the two that touch neither LVGL nor the network: the settings table, and the
# beacon parsers. That is not a coincidence -- port_ble.h yields raw
# advertisement bytes and leaves the parsing above the port layer precisely so
# that it can be reached from here. Everything else in main/ still is not,
# deliberately.
idf_component_register(SRCS "test_runner.cpp"
                            "test_ble_beacon.cpp"
                            "test_config_fields.cpp"
                            "test_item_setters.cpp"
                            "test_ui_theme.cpp"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/ble/ble_beacon.cpp"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/config/config_fields.cpp"
                       INCLUDE_DIRS "."
                                    "${CMAKE_CURRENT_LIST_DIR}/../../../main"
                       REQUIRES unity ArduinoJson freertos)
