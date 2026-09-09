# config_fields.cpp is compiled straight out of main/ rather than mocked: it is
# the one part of the settings that touches neither LVGL nor the network, which
# is what makes the table testable at all. Everything else in main/ still is
# not reachable from here, deliberately.
idf_component_register(SRCS "test_runner.cpp"
                            "test_config_fields.cpp"
                            "test_item_setters.cpp"
                            "test_ui_theme.cpp"
                            "${CMAKE_CURRENT_LIST_DIR}/../../../main/config/config_fields.cpp"
                       INCLUDE_DIRS "."
                                    "${CMAKE_CURRENT_LIST_DIR}/../../../main"
                       REQUIRES unity ArduinoJson freertos)
