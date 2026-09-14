/* Unit tests for reading and writing config.json.
 *
 * Config::loadConfig() and Config::saveConfig() used to be two hand-written
 * copies of the settings list; they are one pass over config_fields[] now,
 * using the JSON path and the default each row carries. That makes the file
 * format *data*, and data with no test is a format that changes by accident:
 * a mistyped path in the table would move a setting to a new place in the
 * file, and the only symptom on a real panel is one value quietly reverting
 * to its default after a reboot.
 *
 * So these tests hold the format still. The literal JSON below is the shipped
 * data/config.json, and every path in it is asserted rather than derived --
 * if a row's json_path changes, this fails and says so, which is exactly what
 * should happen to a change that would strand every deployed panel's file.
 *
 * This links main/config/config.cpp and the two halves of port_storage, so it
 * writes real files. It makes a fresh directory under $TMPDIR for them and
 * points $OHEZ_CONFIG_DIR at it before the first port_storage call -- which
 * matters more than it looks: without it port_storage_dir() falls back to
 * $XDG_CONFIG_HOME/oh-ez-touch, and running the suite would read and then
 * overwrite the config of a simulator the developer actually uses.
 */

#include <unity.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config/config.hpp"
#include "config/config_fields.hpp"
#include "test_suites.hpp"

#define TEST_CONFIG_FILE "config.json"

/* The shipped defaults, as they appear in data/config.json. Deliberately not
 * built from the table: the point is to check the table against the format,
 * so one of the two has to be written out by hand. */
static const char shipped_json[] =
    "{"
    "\"general\":{\"hostname\":\"oheztouch-new\"},"
    "\"ntp\":{\"hostname\":\"pool.ntp.org\",\"gmt_offset\":1,\"daylightsaving\":false},"
    "\"ui\":{\"theme\":\"Default\",\"night_mode\":\"off\",\"night_from\":22,\"night_to\":6},"
    "\"backlight\":{\"activity_timeout\":60,\"normal_brightness\":100,"
    "\"dim_brightness\":40},"
    "\"beeper\":{\"enabled\":true,\"volume\":25},"
    "\"mqtt\":{\"enabled\":false,\"hostname\":\"mosquitto\",\"port\":1883,\"user\":\"\","
    "\"password\":\"\",\"topic\":\"oheztouch\",\"interval\":60,\"retain\":true},"
    "\"ble\":{\"enabled\":false,\"interval\":30,\"window\":5,\"rssi_min\":-90,"
    "\"expire\":120,\"publish_all\":false},"
    "\"openhab\":{\"hostname\":\"openhabian\",\"port\":8080,\"sitemap\":\"oheztouch\"},"
    "\"sensors\":{\"bme280\":{\"use\":false,\"interval\":180}}"
    "}";

/* ---------------------------------------------------------------- fixtures */

/* The directory both this file and port_storage use, created on first call.
 *
 * Set through the environment rather than passed, because port_storage_dir()
 * is what reads it and it has no other input. It is also cached there on its
 * first call, so this has to run before anything touches the store -- hence
 * the call at the top of test_config_file_run(). */
static const char *test_config_dir(void)
{
    static char dir[256];

    if (dir[0] != '\0')
        return dir;

    const char *tmp = getenv("TMPDIR");

    snprintf(dir, sizeof(dir), "%s/ohez-config-test-XXXXXX",
             (tmp != NULL && tmp[0] != '\0') ? tmp : "/tmp");

    TEST_ASSERT_NOT_NULL_MESSAGE(mkdtemp(dir), "cannot make a config directory");

    /* Overwrite: an OHEZ_CONFIG_DIR inherited from the shell would put the
     * fixtures somewhere the developer chose, which is the whole thing this
     * is here to prevent. */
    setenv("OHEZ_CONFIG_DIR", dir, 1);

    return dir;
}

static void config_dir_path(char *buf, size_t size, const char *name)
{
    snprintf(buf, size, "%s/%s", test_config_dir(), name);
}

static void write_file(const char *json)
{
    char  path[512];
    FILE *f;

    config_dir_path(path, sizeof(path), TEST_CONFIG_FILE);

    f = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    TEST_ASSERT_EQUAL_size_t(strlen(json), fwrite(json, 1, strlen(json), f));
    TEST_ASSERT_EQUAL_INT(0, fclose(f));
}

static void remove_file(void)
{
    char path[512];

    config_dir_path(path, sizeof(path), TEST_CONFIG_FILE);
    remove(path);
}

/* Read the file back as a NUL-terminated string. */
static void read_file(char *buf, size_t size)
{
    char  path[512];
    FILE *f;

    config_dir_path(path, sizeof(path), TEST_CONFIG_FILE);

    f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);

    size_t got = fread(buf, 1, size - 1, f);

    buf[got] = '\0';
    fclose(f);
}

/* A Config that has mounted the store. Static because it carries a mutex the
 * tests have no reason to recreate. */
static Config &config_instance(void)
{
    static Config config;
    static bool   ready = false;

    if (ready == false)
    {
        TEST_ASSERT_TRUE_MESSAGE(config.setup(), "config store did not mount");
        ready = true;
    }

    return config;
}

/* ------------------------------------------------------------------ tests */

/* Every default reaches Config from the table, with no file at all. */
static void test_no_file_gives_the_built_in_defaults(void)
{
    Config &config = config_instance();

    remove_file();

    /* False: the settings are usable, but they did not come from a file. That
     * distinction is what tells a caller whether to offer a first-run
     * wizard. */
    TEST_ASSERT_FALSE(config.loadConfig(TEST_CONFIG_FILE));

    TEST_ASSERT_EQUAL_STRING("oheztouch-new", config.item.general.hostname);
    TEST_ASSERT_EQUAL_STRING("pool.ntp.org", config.item.ntp.hostname);
    TEST_ASSERT_EQUAL_INT(1, config.item.ntp.gmt_offset);
    TEST_ASSERT_EQUAL_INT(UI_THEME_DEFAULT, config.item.ui.theme);
    TEST_ASSERT_EQUAL_INT(UI_NIGHT_OFF, config.item.ui.night_mode);
    TEST_ASSERT_EQUAL_UINT(22, config.item.ui.night_from);
    TEST_ASSERT_EQUAL_INT(100, config.item.backlight.normal_brightness);
    TEST_ASSERT_TRUE(config.item.beeper.enabled);
    TEST_ASSERT_EQUAL_UINT(25, config.item.beeper.volume);
    TEST_ASSERT_FALSE(config.item.mqtt.enabled);
    TEST_ASSERT_EQUAL_INT(1883, config.item.mqtt.port);
    TEST_ASSERT_EQUAL_STRING("openhabian", config.item.openhab.hostname);
    TEST_ASSERT_EQUAL_INT(8080, config.item.openhab.port);
    TEST_ASSERT_FALSE(config.item.sensors.bme280.use);
    TEST_ASSERT_EQUAL_INT(180, config.item.sensors.bme280.interval);
    TEST_ASSERT_EQUAL_INT(-90, config.item.ble.rssi_min);
}

/* The shipped file loads, and every path in it is the one the table names. */
static void test_the_shipped_file_loads(void)
{
    Config &config = config_instance();

    write_file(shipped_json);

    TEST_ASSERT_TRUE(config.loadConfig(TEST_CONFIG_FILE));

    /* Its one difference from the built-in defaults, which is also what makes
     * this a real test of the path rather than of the fallback. */
    TEST_ASSERT_EQUAL_STRING("oheztouch", config.item.openhab.sitemap);
}

/* One value per section, none of them a default, all read back. This is what
 * catches a json_path typo: a wrong path reads as "absent", which silently
 * leaves the default -- so every assertion here is against a value that
 * differs from it. */
static void test_every_section_round_trips(void)
{
    Config &config = config_instance();

    static const char json[] =
        "{"
        "\"general\":{\"hostname\":\"hall-panel\"},"
        "\"ntp\":{\"hostname\":\"ntp.lan\",\"gmt_offset\":-5,\"daylightsaving\":true},"
        "\"ui\":{\"theme\":\"LCARS\",\"night_mode\":\"auto\",\"night_from\":21,"
        "\"night_to\":7},"
        "\"backlight\":{\"activity_timeout\":30,\"normal_brightness\":80,"
        "\"dim_brightness\":10},"
        "\"beeper\":{\"enabled\":false,\"volume\":80},"
        "\"mqtt\":{\"enabled\":true,\"hostname\":\"broker.lan\",\"port\":8883,"
        "\"user\":\"panel\",\"password\":\"sekrit\",\"topic\":\"home/panels\","
        "\"interval\":15,\"retain\":false},"
        "\"ble\":{\"enabled\":true,\"interval\":45,\"window\":9,\"rssi_min\":-70,"
        "\"expire\":300,\"publish_all\":true},"
        "\"openhab\":{\"hostname\":\"oh.lan\",\"port\":8090,\"sitemap\":\"panel\"},"
        "\"sensors\":{\"bme280\":{\"use\":true,\"interval\":90}}"
        "}";

    write_file(json);
    TEST_ASSERT_TRUE(config.loadConfig(TEST_CONFIG_FILE));

    TEST_ASSERT_EQUAL_STRING("hall-panel", config.item.general.hostname);

    TEST_ASSERT_EQUAL_STRING("ntp.lan", config.item.ntp.hostname);
    TEST_ASSERT_EQUAL_INT(-5, config.item.ntp.gmt_offset);
    TEST_ASSERT_TRUE(config.item.ntp.daylightsaving);

    TEST_ASSERT_EQUAL_INT(UI_THEME_LCARS, config.item.ui.theme);
    TEST_ASSERT_EQUAL_INT(UI_NIGHT_AUTO, config.item.ui.night_mode);
    TEST_ASSERT_EQUAL_UINT(21, config.item.ui.night_from);
    TEST_ASSERT_EQUAL_UINT(7, config.item.ui.night_to);

    TEST_ASSERT_EQUAL_UINT32(30, config.item.backlight.activity_timeout);
    TEST_ASSERT_EQUAL_UINT(80, config.item.backlight.normal_brightness);
    TEST_ASSERT_EQUAL_UINT(10, config.item.backlight.dim_brightness);

    TEST_ASSERT_FALSE(config.item.beeper.enabled);
    TEST_ASSERT_EQUAL_UINT(80, config.item.beeper.volume);

    TEST_ASSERT_TRUE(config.item.mqtt.enabled);
    TEST_ASSERT_EQUAL_STRING("broker.lan", config.item.mqtt.hostname);
    TEST_ASSERT_EQUAL_INT(8883, config.item.mqtt.port);
    TEST_ASSERT_EQUAL_STRING("panel", config.item.mqtt.user);
    TEST_ASSERT_EQUAL_STRING("sekrit", config.item.mqtt.password);
    TEST_ASSERT_EQUAL_STRING("home/panels", config.item.mqtt.topic);
    TEST_ASSERT_EQUAL_INT(15, config.item.mqtt.interval);
    TEST_ASSERT_FALSE(config.item.mqtt.retain);

    TEST_ASSERT_TRUE(config.item.ble.enabled);
    TEST_ASSERT_EQUAL_INT(45, config.item.ble.interval);
    TEST_ASSERT_EQUAL_INT(9, config.item.ble.window);
    TEST_ASSERT_EQUAL_INT(-70, config.item.ble.rssi_min);
    TEST_ASSERT_EQUAL_INT(300, config.item.ble.expire);
    TEST_ASSERT_TRUE(config.item.ble.publish_all);

    TEST_ASSERT_EQUAL_STRING("oh.lan", config.item.openhab.hostname);
    TEST_ASSERT_EQUAL_INT(8090, config.item.openhab.port);
    TEST_ASSERT_EQUAL_STRING("panel", config.item.openhab.sitemap);

    TEST_ASSERT_TRUE(config.item.sensors.bme280.use);
    TEST_ASSERT_EQUAL_INT(90, config.item.sensors.bme280.interval);

    /* And back out to the file, then in again: what saveConfig() writes has to
     * be something loadConfig() reads the same way, which is the property the
     * two sharing one table is supposed to guarantee. */
    config_item_t saved = config.item;

    TEST_ASSERT_TRUE(config.saveConfig());

    memset(&config.item, 0, sizeof(config.item));

    TEST_ASSERT_TRUE(config.loadConfig(TEST_CONFIG_FILE));
    TEST_ASSERT_EQUAL_MEMORY(&saved, &config.item, sizeof(saved));
}

/* The file saveConfig() writes has to carry the paths, not just round trip
 * through code that agrees with itself. */
static void test_saved_file_has_the_expected_paths(void)
{
    Config &config = config_instance();
    char    buf[CONFIG_FILE_MAX_SIZE];

    remove_file();
    TEST_ASSERT_FALSE(config.loadConfig(TEST_CONFIG_FILE));
    TEST_ASSERT_TRUE(config.saveConfig());

    read_file(buf, sizeof(buf));

    /* Compact, one object per section, and the BME280 nested a second level
     * down -- which is the one path in the table that is not flat. */
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"general\":{\"hostname\":\"oheztouch-new\"}"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"sensors\":{\"bme280\":{"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"activity_timeout\":60"));

    /* A theme is stored by name, never by index: the numbering may move
     * between firmware versions and the name may not. */
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"theme\":\"Default\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"night_mode\":\"off\""));

    /* A bool is a JSON bool, so the file stays hand-editable. */
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"enabled\":true"));
    TEST_ASSERT_NULL(strstr(buf, "\"enabled\":1"));

    /* It has to stay inside the buffer loadConfig() is willing to read, with
     * enough headroom that the next setting added does not silently push
     * every value back to its default. */
    TEST_ASSERT_TRUE(strlen(buf) < CONFIG_FILE_MAX_SIZE / 2);
}

/* A file that is not JSON at all leaves a complete set of defaults, not a
 * half-applied mixture of the two. */
static void test_a_corrupt_file_gives_the_defaults(void)
{
    Config &config = config_instance();

    write_file("{\"mqtt\":{\"hostname\":\"broker.lan\",\"port\":");

    TEST_ASSERT_FALSE(config.loadConfig(TEST_CONFIG_FILE));

    TEST_ASSERT_EQUAL_STRING("mosquitto", config.item.mqtt.hostname);
    TEST_ASSERT_EQUAL_INT(1883, config.item.mqtt.port);
    TEST_ASSERT_EQUAL_STRING("oheztouch-new", config.item.general.hostname);
}

/* A section the file omits keeps its defaults, and the sections it does carry
 * are still applied. An older firmware's file is exactly this shape. */
static void test_missing_sections_keep_their_defaults(void)
{
    Config &config = config_instance();

    write_file("{\"openhab\":{\"hostname\":\"oh.lan\"}}");

    TEST_ASSERT_TRUE(config.loadConfig(TEST_CONFIG_FILE));

    TEST_ASSERT_EQUAL_STRING("oh.lan", config.item.openhab.hostname);
    /* Same section, absent key. */
    TEST_ASSERT_EQUAL_INT(8080, config.item.openhab.port);
    /* Absent section. */
    TEST_ASSERT_EQUAL_STRING("mosquitto", config.item.mqtt.hostname);
    TEST_ASSERT_EQUAL_INT(180, config.item.sensors.bme280.interval);
}

/* A hand-edited file goes through the same validation the web form and the
 * settings screen apply, which the old hand-written loader did not do. */
static void test_a_hand_edited_file_is_validated(void)
{
    Config &config = config_instance();

    write_file("{\"openhab\":{\"hostname\":\"http://oh.lan\",\"port\":99999},"
               "\"backlight\":{\"normal_brightness\":250},"
               "\"beeper\":{\"volume\":500}}");

    TEST_ASSERT_TRUE(config.loadConfig(TEST_CONFIG_FILE));

    /* '/' and ':' are refused on a SETTINGS_F_HOSTCHARS row, so the default
     * stands rather than a URL reaching the place a host name goes. */
    TEST_ASSERT_EQUAL_STRING("openhabian", config.item.openhab.hostname);

    /* Numbers are clamped to the row's range rather than rejected. */
    TEST_ASSERT_EQUAL_INT(65535, config.item.openhab.port);
    TEST_ASSERT_EQUAL_UINT(100, config.item.backlight.normal_brightness);
    TEST_ASSERT_EQUAL_UINT(100, config.item.beeper.volume);
}

/* An unknown theme name selects the default rather than an out-of-range enum,
 * which every reader of ui.theme then indexes a table with. */
static void test_an_unknown_enum_name_falls_back(void)
{
    Config &config = config_instance();

    write_file("{\"ui\":{\"theme\":\"Hologram\",\"night_mode\":\"maybe\"}}");

    TEST_ASSERT_TRUE(config.loadConfig(TEST_CONFIG_FILE));

    TEST_ASSERT_EQUAL_INT(UI_THEME_DEFAULT, config.item.ui.theme);
    TEST_ASSERT_EQUAL_INT(UI_NIGHT_OFF, config.item.ui.night_mode);

    /* And case does not matter, as it does not in the web form's POST. */
    write_file("{\"ui\":{\"theme\":\"lcars\"}}");
    TEST_ASSERT_TRUE(config.loadConfig(TEST_CONFIG_FILE));
    TEST_ASSERT_EQUAL_INT(UI_THEME_LCARS, config.item.ui.theme);
}

/* A hand-written 1 or 0 for a boolean is honoured.
 *
 * This is a trap the old loader had. It read booleans as `doc[...] | false`,
 * and ArduinoJson 7's `|` refuses to coerce an integer to a bool -- so every
 * `"enabled": 1` in a hand-edited file fell back to the default and was
 * silently ignored, which cost a debugging session. The shipped
 * data/config.json got away with it only because each value happened to equal
 * its fallback.
 *
 * as<bool>() does coerce, so this now works. saveConfig() still writes real
 * true/false; this is about what a human may type. */
static void test_a_hand_written_integer_boolean_is_honoured(void)
{
    Config &config = config_instance();

    write_file("{\"mqtt\":{\"enabled\":1,\"retain\":0},\"beeper\":{\"enabled\":0},"
               "\"sensors\":{\"bme280\":{\"use\":1}}}");

    TEST_ASSERT_TRUE(config.loadConfig(TEST_CONFIG_FILE));

    /* Each of these differs from its default, so a fallback would show. */
    TEST_ASSERT_TRUE(config.item.mqtt.enabled);   /* default false */
    TEST_ASSERT_FALSE(config.item.mqtt.retain);   /* default true  */
    TEST_ASSERT_FALSE(config.item.beeper.enabled); /* default true  */
    TEST_ASSERT_TRUE(config.item.sensors.bme280.use); /* default false */
}

/* And a number written as a string, which is the other thing a human types. */
static void test_a_quoted_number_is_honoured(void)
{
    Config &config = config_instance();

    write_file("{\"openhab\":{\"port\":\"8090\"},\"mqtt\":{\"interval\":\"30\"}}");

    TEST_ASSERT_TRUE(config.loadConfig(TEST_CONFIG_FILE));

    TEST_ASSERT_EQUAL_INT(8090, config.item.openhab.port);
    TEST_ASSERT_EQUAL_INT(30, config.item.mqtt.interval);
}

/* saveConfig() before any load has no file name to write to, and must say so
 * rather than inventing one. */
static void test_save_without_a_load_fails(void)
{
    Config fresh;

    TEST_ASSERT_TRUE(fresh.setup());
    TEST_ASSERT_FALSE(fresh.saveConfig());
}

void test_config_file_run(void)
{
    /* Before the first Config::setup(): port_storage caches the directory. */
    test_config_dir();

    RUN_TEST(test_no_file_gives_the_built_in_defaults);
    RUN_TEST(test_the_shipped_file_loads);
    RUN_TEST(test_every_section_round_trips);
    RUN_TEST(test_saved_file_has_the_expected_paths);
    RUN_TEST(test_a_corrupt_file_gives_the_defaults);
    RUN_TEST(test_missing_sections_keep_their_defaults);
    RUN_TEST(test_a_hand_edited_file_is_validated);
    RUN_TEST(test_an_unknown_enum_name_falls_back);
    RUN_TEST(test_a_hand_written_integer_boolean_is_honoured);
    RUN_TEST(test_a_quoted_number_is_honoured);
    RUN_TEST(test_save_without_a_load_fails);

    /* The directory itself is left behind: it is one empty directory under
     * $TMPDIR, and removing it would need the same care as creating it for a
     * gain nobody measures. */
    remove_file();
}
