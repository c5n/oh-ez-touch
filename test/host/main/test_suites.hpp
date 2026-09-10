/**
 * @file test_suites.hpp
 *
 * The suites, for test_runner.cpp to call.
 *
 * Each file used to have a main() of its own, because PlatformIO builds one
 * binary per test directory. IDF's unity app is one binary, so each file
 * declares its RUN_TEST list here instead and the runner owns UNITY_BEGIN and
 * UNITY_END.
 */
#ifndef TEST_SUITES_HPP
#define TEST_SUITES_HPP

void test_ble_beacon_run(void);
void test_config_fields_run(void);
void test_config_file_run(void);
void test_item_setters_run(void);
void test_multipart_run(void);
void test_item_state_run(void);
void test_item_urls_run(void);
void test_outputs_run(void);
void test_sitemap_parse_run(void);
void test_ui_geometry_run(void);
void test_ui_theme_run(void);

#endif /* TEST_SUITES_HPP */
