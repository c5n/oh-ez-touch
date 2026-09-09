idf_component_register(SRCS "test_runner.cpp"
                            "test_item_setters.cpp"
                            "test_ui_theme.cpp"
                       INCLUDE_DIRS "."
                                    "${CMAKE_CURRENT_LIST_DIR}/../../../main"
                       REQUIRES unity ArduinoJson)
