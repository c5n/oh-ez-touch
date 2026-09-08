# The application component. src/ moves in here in a later commit; for now this
# is a skeleton whose only job is to prove that the three project-owned
# components build and link on both targets.
idf_component_register(SRCS "main.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES lvgl lodepng ArduinoJson)
