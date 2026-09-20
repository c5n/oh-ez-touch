# ArduinoJson, header-only. It has no Arduino dependency of its own: with
# ARDUINOJSON_ENABLE_ARDUINO_* off it uses only <string.h>/<stdlib.h> and the
# standard streams, which is why it survives the move off the Arduino core.
cmake_minimum_required(VERSION 3.16)

set(ARDUINOJSON_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/ArduinoJson")
if(NOT EXISTS "${ARDUINOJSON_ROOT_DIR}/src/ArduinoJson.h")
    message(FATAL_ERROR
        "components/ArduinoJson/ArduinoJson is empty. Run: git submodule update --init --recursive")
endif()

idf_component_register(INCLUDE_DIRS "${ARDUINOJSON_ROOT_DIR}/src" REQUIRES "")

target_compile_definitions(${COMPONENT_LIB} INTERFACE
    ARDUINOJSON_ENABLE_ARDUINO_STRING=0
    ARDUINOJSON_ENABLE_ARDUINO_STREAM=0
    ARDUINOJSON_ENABLE_ARDUINO_PRINT=0
    ARDUINOJSON_ENABLE_PROGMEM=0)
