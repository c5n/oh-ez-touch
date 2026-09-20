/* Unit tests for the REST API's JSON core, main/web/webui_api_json.cpp.
 *
 * The handlers are transport and a lock; everything they *decide* -- what a
 * status or a config document contains, what a POST accepts, what a rejected
 * POST rolls back -- is in that file, and is what these tests pin down. It
 * includes nothing a host build cannot link, the same split multipart.c got.
 *
 * Run with:
 *   cd test/host && idf.py build && ./build/oh-ez-touch-host-test.elf
 */

#include <unity.h>

#include <ArduinoJson.h>

#include <string.h>

#include "web/webui_api_json.hpp"
#include "test_suites.hpp"

/* A settings struct to read and write. Same argument as test_config_fields:
 * not a Config, because none of this goes near the mutex or the file. */
static config_item_t item;

static void set_defaults(void)
{
    config_fields_set_defaults(&item);
}

/* The response buffer the apply contract sizes: the request plus the fixed
 * overhead. Tests use it exactly as the handler does. */
static char response[4096 + WEBUI_API_APPLY_RESPONSE_OVERHEAD];

/* Parse a produced document back, so assertions read as JSON rather than as
 * substring hunts that would also match inside an escaped value. */
static void parse(JsonDocument &doc, const char *json)
{
    TEST_ASSERT_TRUE(deserializeJson(doc, json) == DeserializationError::Ok);
}

/* ------------------------------------------------------------------ status */

static void test_status_has_the_fleet_fields(void)
{
    struct webui_api_status_s st = {};

    strcpy(st.version, "0.91");
    st.target = "Lanbon";
    st.build = "Sep 20 2026 10:00:00";
    st.uptime_s = 90061;
    st.hostname = "oheztouch-01";
    st.ssid = "HomeNet";
    st.wired = false;
    st.rssi = -55;
    st.ip = "192.168.1.50";
    st.mac = "aa:bb:cc:dd:ee:ff";
    st.free_heap = 123456;

    size_t need = webui_api_status_json(&st, NULL, 0);

    char buf[512];

    TEST_ASSERT_LESS_THAN(sizeof(buf), need + 1);

    size_t written = webui_api_status_json(&st, buf, sizeof(buf));

    TEST_ASSERT_EQUAL(need, written);

    JsonDocument doc;

    parse(doc, buf);

    TEST_ASSERT_EQUAL_STRING("0.91", doc["version"]);
    TEST_ASSERT_EQUAL_STRING("Lanbon", doc["target"]);
    TEST_ASSERT_EQUAL(90061, doc["uptime_s"].as<unsigned long long>());
    TEST_ASSERT_EQUAL_STRING("oheztouch-01", doc["hostname"]);
    TEST_ASSERT_EQUAL_STRING("HomeNet", doc["ssid"]);
    TEST_ASSERT_FALSE(doc["wired"]);
    TEST_ASSERT_EQUAL(-55, doc["rssi"]);
    TEST_ASSERT_EQUAL_STRING("192.168.1.50", doc["ip"]);
    TEST_ASSERT_EQUAL_STRING("aa:bb:cc:dd:ee:ff", doc["mac"]);
    TEST_ASSERT_EQUAL(123456, doc["free_heap"]);
}

static void test_status_omits_rssi_when_wired(void)
{
    struct webui_api_status_s st = {};

    strcpy(st.version, "0.91");
    st.target = "Lanbon";
    st.build = "build";
    st.wired = true;
    st.hostname = "";
    st.ssid = "eth0";
    st.ip = "";
    st.mac = "";

    char buf[512];

    webui_api_status_json(&st, buf, sizeof(buf));

    JsonDocument doc;

    parse(doc, buf);

    TEST_ASSERT_TRUE(doc["wired"]);
    TEST_ASSERT_TRUE(doc["rssi"].isNull());
}

/* ------------------------------------------------------------------ config */

/* The one row with each shape, looked up once per test rather than trusted
 * to be there. */
static const struct config_field_s *row(const char *name)
{
    return config_field_by_name(name);
}

static void test_config_lists_every_field_once(void)
{
    set_defaults();

    size_t need = webui_api_config_json(&item, NULL, 0);

    char *buf = new char[need + 1];

    webui_api_config_json(&item, buf, need + 1);

    JsonDocument doc;

    parse(doc, buf);

    JsonArray fields = doc["fields"];

    size_t named = 0, sections = 0;

    for (JsonObject f : fields)
    {
        if (f["section"].isNull() == false)
        {
            sections++;

            /* A section names its tab, from the same table the touch screen
             * titles its pages from -- devmgr groups the form into tabs by
             * it. */
            bool known = false;

            for (size_t t = 0; t < SETTINGS_TAB_COUNT; t++)
                if (strcmp(f["tab"] | "", settings_tab_names[t]) == 0)
                    known = true;

            TEST_ASSERT_TRUE(known);
            continue;
        }

        named++;

        /* A client renders the form from this alone: name, label, kind and
         * value are the minimum that takes. */
        TEST_ASSERT_FALSE(f["name"].isNull());
        TEST_ASSERT_FALSE(f["label"].isNull());
        TEST_ASSERT_FALSE(f["kind"].isNull());
        TEST_ASSERT_FALSE(f["value"].isNull());

        TEST_ASSERT_NOT_NULL(row(f["name"]));
    }

    size_t expect_named = 0, expect_sections = 0;

    for (size_t i = 0; i < config_field_count; i++)
    {
        if (config_fields[i].kind == SETTINGS_SECTION)
            expect_sections++;
        else
            expect_named++;
    }

    TEST_ASSERT_EQUAL(expect_named, named);
    TEST_ASSERT_EQUAL(expect_sections, sections);

    delete[] buf;
}

static void test_config_values_are_typed(void)
{
    set_defaults();

    size_t need = webui_api_config_json(&item, NULL, 0);

    char *buf = new char[need + 1];

    webui_api_config_json(&item, buf, need + 1);

    JsonDocument doc;

    parse(doc, buf);

    for (JsonObject f : doc["fields"].as<JsonArray>())
    {
        if (f["section"].isNull() == false)
            continue;

        const struct config_field_s *r = row(f["name"]);

        switch (r->kind)
        {
        case SETTINGS_TEXT:
            TEST_ASSERT_TRUE(f["value"].is<const char *>());
            break;
        case SETTINGS_BOOL:
            TEST_ASSERT_TRUE(f["value"].is<bool>());
            break;
        case SETTINGS_ENUM:
            TEST_ASSERT_TRUE(f["value"].is<const char *>());
            TEST_ASSERT_EQUAL(r->count, f["options"].size());
            break;
        default:
            TEST_ASSERT_TRUE(f["value"].is<long>());
            TEST_ASSERT_EQUAL(r->min, f["min"]);
            TEST_ASSERT_EQUAL(r->max, f["max"]);
            break;
        }
    }

    delete[] buf;
}

static void test_config_masks_the_secret(void)
{
    set_defaults();

    config_field_set_text(row("mqtt_pass"), &item, "hunter2");

    size_t need = webui_api_config_json(&item, NULL, 0);

    char *buf = new char[need + 1];

    webui_api_config_json(&item, buf, need + 1);

    /* Nowhere in the document, parsed or raw -- the mask is the whole point. */
    TEST_ASSERT_NULL(strstr(buf, "hunter2"));

    JsonDocument doc;

    parse(doc, buf);

    for (JsonObject f : doc["fields"].as<JsonArray>())
    {
        if (f["name"].isNull() || strcmp(f["name"], "mqtt_pass") != 0)
            continue;

        TEST_ASSERT_EQUAL_STRING("***", f["value"]);
        TEST_ASSERT_TRUE(f["secret"]);

        delete[] buf;
        return;
    }

    TEST_FAIL_MESSAGE("mqtt_pass not in the field list");
}

/* ------------------------------------------------------------ config apply */

/* The enum's current option name, as the API would render it. */
static const char *enum_value_name(const struct config_field_s *f, const config_item_t *it)
{
    int32_t index = config_field_read(f, it);

    if (index < 0 || index >= (int32_t)f->count)
        index = 0;

    return f->names[index];
}

static void test_apply_changes_what_it_names(void)
{
    set_defaults();

    const char *body = "{\"oh_host\":\"openhabian2\",\"oh_port\":8443,"
                       "\"beeper\":false,\"theme\":\"LCARS\"}";

    bool ok = webui_api_config_apply(&item, body, strlen(body),
                                     response, sizeof(response));

    TEST_ASSERT_TRUE(ok);

    TEST_ASSERT_EQUAL_STRING("openhabian2", config_field_text(row("oh_host"), &item));
    TEST_ASSERT_EQUAL(8443, config_field_read(row("oh_port"), &item));
    TEST_ASSERT_EQUAL(0, config_field_read(row("beeper"), &item));
    TEST_ASSERT_EQUAL_STRING("LCARS", enum_value_name(row("theme"), &item));

    JsonDocument doc;

    parse(doc, response);

    TEST_ASSERT_EQUAL(4, doc["applied"].size());
    TEST_ASSERT_TRUE(doc["rejected"].isNull());
}

static void test_apply_leaves_absent_fields_alone(void)
{
    set_defaults();

    config_field_set_text(row("mqtt_host"), &item, "broker.example");
    config_field_write(row("mqtt_use"), &item, 1);

    /* One field in the body, and the checkboxes are the point: unlike the
     * form's /save, an absent one must NOT read as "off". */
    const char *body = "{\"oh_host\":\"oh2\"}";

    TEST_ASSERT_TRUE(webui_api_config_apply(&item, body, strlen(body),
                                            response, sizeof(response)));

    TEST_ASSERT_EQUAL_STRING("broker.example", config_field_text(row("mqtt_host"), &item));
    TEST_ASSERT_EQUAL(1, config_field_read(row("mqtt_use"), &item));
}

static void test_apply_rolls_back_on_any_rejection(void)
{
    set_defaults();

    config_field_set_text(row("oh_host"), &item, "original");

    /* One good field, one bad enum, one unknown name: the good one must not
     * survive the request failing. */
    const char *body = "{\"oh_host\":\"changed\",\"theme\":\"NoSuchTheme\","
                       "\"no_such_setting\":1}";

    bool ok = webui_api_config_apply(&item, body, strlen(body),
                                     response, sizeof(response));

    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_STRING("original", config_field_text(row("oh_host"), &item));

    JsonDocument doc;

    parse(doc, response);

    TEST_ASSERT_EQUAL_STRING("unknown setting", doc["rejected"]["no_such_setting"]);
    TEST_ASSERT_EQUAL_STRING("value not accepted", doc["rejected"]["theme"]);
    TEST_ASSERT_TRUE(doc["applied"].isNull());
}

static void test_apply_rejects_a_hostchars_violation(void)
{
    set_defaults();

    const char *body = "{\"oh_host\":\"has/slash\"}";

    TEST_ASSERT_FALSE(webui_api_config_apply(&item, body, strlen(body),
                                             response, sizeof(response)));

    TEST_ASSERT_EQUAL_STRING("openhabian", config_field_text(row("oh_host"), &item));
}

static void test_apply_bool_spellings(void)
{
    set_defaults();

    const char *body = "{\"mqtt_use\":true,\"mqtt_retain\":\"off\","
                       "\"beeper\":\"ON\",\"bme_use\":0}";

    TEST_ASSERT_TRUE(webui_api_config_apply(&item, body, strlen(body),
                                            response, sizeof(response)));

    TEST_ASSERT_EQUAL(1, config_field_read(row("mqtt_use"), &item));
    TEST_ASSERT_EQUAL(0, config_field_read(row("mqtt_retain"), &item));
    TEST_ASSERT_EQUAL(1, config_field_read(row("beeper"), &item));
    TEST_ASSERT_EQUAL(0, config_field_read(row("bme_use"), &item));
}

static void test_apply_clamps_a_number(void)
{
    set_defaults();

    const char *body = "{\"oh_port\":70000}";

    TEST_ASSERT_TRUE(webui_api_config_apply(&item, body, strlen(body),
                                            response, sizeof(response)));

    TEST_ASSERT_EQUAL(65535, config_field_read(row("oh_port"), &item));
}

static void test_apply_secret_mask_is_a_noop(void)
{
    set_defaults();

    config_field_set_text(row("mqtt_pass"), &item, "hunter2");

    /* The masked echo a client sends back must not overwrite the password --
     * and must not fail the request either: it is not a change. */
    const char *body = "{\"mqtt_pass\":\"***\",\"mqtt_host\":\"b2\"}";

    TEST_ASSERT_TRUE(webui_api_config_apply(&item, body, strlen(body),
                                            response, sizeof(response)));

    TEST_ASSERT_EQUAL_STRING("hunter2", config_field_text(row("mqtt_pass"), &item));
    TEST_ASSERT_EQUAL_STRING("b2", config_field_text(row("mqtt_host"), &item));

    JsonDocument doc;

    parse(doc, response);

    TEST_ASSERT_EQUAL(1, doc["applied"].size());
}

static void test_apply_secret_accepts_a_real_value(void)
{
    set_defaults();

    const char *body = "{\"mqtt_pass\":\"s3cret\"}";

    TEST_ASSERT_TRUE(webui_api_config_apply(&item, body, strlen(body),
                                            response, sizeof(response)));

    TEST_ASSERT_EQUAL_STRING("s3cret", config_field_text(row("mqtt_pass"), &item));
}

static void test_apply_rejects_a_non_object(void)
{
    set_defaults();

    const char *body = "[1,2,3]";

    TEST_ASSERT_FALSE(webui_api_config_apply(&item, body, strlen(body),
                                             response, sizeof(response)));

    JsonDocument doc;

    parse(doc, response);

    TEST_ASSERT_FALSE(doc["error"].isNull());
}

/* ------------------------------------------------------------------- sound */

static void test_sounds_lists_the_vocabulary(void)
{
    size_t need = webui_api_sounds_json(NULL, 0);

    char *buf = new char[need + 1];

    webui_api_sounds_json(buf, need + 1);

    JsonDocument doc;

    parse(doc, buf);

    TEST_ASSERT_EQUAL(UI_SOUND_COUNT, doc["sounds"].size());
    TEST_ASSERT_EQUAL_STRING("door_chime", doc["sounds"][UI_SOUND_DOOR_CHIME]);

    delete[] buf;
}

static void test_sound_parse_accepts_door_chime(void)
{
    enum ui_sound_e sound = UI_SOUND_COUNT;
    bool            force = false;

    const char *body = "{\"name\":\"door_chime\",\"force\":true}";

    bool ok = webui_api_sound_parse(body, strlen(body), &sound, &force,
                                    response, sizeof(response));

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(UI_SOUND_DOOR_CHIME, sound);
    TEST_ASSERT_TRUE(force);

    JsonDocument doc;

    parse(doc, response);

    TEST_ASSERT_EQUAL_STRING("door_chime", doc["queued"]);
}

static void test_sound_parse_defaults_force_off(void)
{
    enum ui_sound_e sound = UI_SOUND_COUNT;
    bool            force = true;

    const char *body = "{\"name\":\"accept\"}";

    TEST_ASSERT_TRUE(webui_api_sound_parse(body, strlen(body), &sound, &force,
                                           response, sizeof(response)));

    TEST_ASSERT_EQUAL(UI_SOUND_ACCEPT, sound);
    TEST_ASSERT_FALSE(force);
}

static void test_sound_parse_names_the_vocabulary_on_a_miss(void)
{
    enum ui_sound_e sound = UI_SOUND_COUNT;
    bool            force = false;

    const char *body = "{\"name\":\"dooor_chime\"}";

    bool ok = webui_api_sound_parse(body, strlen(body), &sound, &force,
                                    response, sizeof(response));

    TEST_ASSERT_FALSE(ok);

    JsonDocument doc;

    parse(doc, response);

    TEST_ASSERT_EQUAL_STRING("unknown sound", doc["error"]);
    TEST_ASSERT_EQUAL(UI_SOUND_COUNT, doc["sounds"].size());
}

void test_webui_api_run(void)
{
    RUN_TEST(test_status_has_the_fleet_fields);
    RUN_TEST(test_status_omits_rssi_when_wired);
    RUN_TEST(test_config_lists_every_field_once);
    RUN_TEST(test_config_values_are_typed);
    RUN_TEST(test_config_masks_the_secret);
    RUN_TEST(test_apply_changes_what_it_names);
    RUN_TEST(test_apply_leaves_absent_fields_alone);
    RUN_TEST(test_apply_rolls_back_on_any_rejection);
    RUN_TEST(test_apply_rejects_a_hostchars_violation);
    RUN_TEST(test_apply_bool_spellings);
    RUN_TEST(test_apply_clamps_a_number);
    RUN_TEST(test_apply_secret_mask_is_a_noop);
    RUN_TEST(test_apply_secret_accepts_a_real_value);
    RUN_TEST(test_apply_rejects_a_non_object);
    RUN_TEST(test_sounds_lists_the_vocabulary);
    RUN_TEST(test_sound_parse_accepts_door_chime);
    RUN_TEST(test_sound_parse_defaults_force_off);
    RUN_TEST(test_sound_parse_names_the_vocabulary_on_a_miss);
}
