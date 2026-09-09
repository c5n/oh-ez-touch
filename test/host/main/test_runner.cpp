/**
 * @file test_runner.cpp
 *
 * One binary, every suite, and an exit status that says how many failed.
 *
 * app_main() does not return here, which is the same rule the firmware's does
 * -- FreeRTOS's linux port calls vTaskDelete(NULL) afterwards and trips an
 * assertion -- but for the opposite reason: this one exits the process, on
 * purpose, so that `idf.py build && ./build/oh-ez-touch-host-test.elf` in a
 * script or a CI job means what it looks like it means.
 */

#include "test_suites.hpp"

#include <stdio.h>
#include <stdlib.h>

#include <unity.h>

/* Unity requires both, and neither suite has any state to set up: every test
 * builds its own fixture on the stack. */
void setUp(void) {}
void tearDown(void) {}

extern "C" void app_main(void)
{
    UNITY_BEGIN();

    test_ble_beacon_run();
    test_config_fields_run();
    test_item_setters_run();
    test_ui_theme_run();

    int failures = UNITY_END();

    fflush(stdout);
    exit(failures);
}
