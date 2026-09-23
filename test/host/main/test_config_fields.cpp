/* Unit tests for the settings table in main/config/config_fields.cpp.
 *
 * The table is walked by three front ends that share nothing else -- the web
 * form, the panel's settings screen and the MQTT client -- and each of them
 * reaches into Config by the byte offset a row carries. So the interesting
 * failures are not in the accessors but in the table: a row whose C type has
 * stopped matching its kind writes over its neighbour, and a row whose name
 * collides with another's makes one of the two unreachable from the browser and
 * from the broker both. static_assert covers the widths; these cover the rest.
 *
 * The MQTT client added a constraint that nothing enforced before it: a field
 * name becomes one segment of a topic, `<prefix>/config/<name>/set`, so a name
 * containing '/', '+' or '#' would produce a topic that is either wrong or
 * illegal -- and the subscription is a wildcard, so the mistake would show up
 * as a setting that silently cannot be set rather than as an error.
 *
 * Host-only, and it links main/config/config_fields.cpp -- the first thing
 * from main/ that this app compiles. It gets away with it because that file is
 * the one part of the settings that touches neither LVGL nor the network, which
 * is what its own header comment promises. Run with:
 *   cd test/host && idf.py build && ./build/oh-ez-touch-host-test.elf
 */

#include <unity.h>

#include <string.h>

#include "config/config_fields.hpp"
#include "test_suites.hpp"

/* A settings struct to write into. Not a Config: that one owns a mutex and a
 * file name, and none of the accessors under test go near either. */
static config_item_t item;

static const struct config_field_s *row(const char *name)
{
    for (size_t i = 0; i < config_field_count; i++)
        if (config_fields[i].kind != SETTINGS_SECTION
            && strcmp(config_fields[i].name, name) == 0)
            return &config_fields[i];

    return NULL;
}

/* ------------------------------------------------------------ the table */

static void test_every_row_is_well_formed(void)
{
    for (size_t i = 0; i < config_field_count; i++)
    {
        const struct config_field_s *f = &config_fields[i];

        /* A label is what both front ends render, so every row needs one --
         * including a section, which is nothing but its label. */
        TEST_ASSERT_NOT_NULL(f->label);
        TEST_ASSERT_TRUE(f->label[0] != '\0');

        if (f->kind == SETTINGS_SECTION)
        {
            TEST_ASSERT_TRUE(f->tab < SETTINGS_TAB_COUNT);
            continue;
        }

        TEST_ASSERT_NOT_NULL(f->name);
        TEST_ASSERT_TRUE(f->name[0] != '\0');

        if (f->kind == SETTINGS_TEXT)
        {
            /* config_field_set_text() copies f->size - 1 characters, so a zero
             * would make strlcpy() write nothing and read a size of -1. */
            TEST_ASSERT_TRUE(f->size > 1);
        }
        else if (f->kind == SETTINGS_ENUM)
        {
            TEST_ASSERT_NOT_NULL(f->names);
            TEST_ASSERT_TRUE(f->count > 0);
            TEST_ASSERT_EQUAL_INT32((int32_t)f->count - 1, f->max);
        }
        else if (f->kind != SETTINGS_BOOL)
        {
            /* config_field_set_number() clamps into [min, max]; inverted
             * bounds would clamp every value to min. */
            TEST_ASSERT_TRUE(f->min <= f->max);
        }
    }
}

static void test_field_names_are_unique(void)
{
    for (size_t i = 0; i < config_field_count; i++)
    {
        if (config_fields[i].kind == SETTINGS_SECTION)
            continue;

        for (size_t j = i + 1; j < config_field_count; j++)
        {
            if (config_fields[j].kind == SETTINGS_SECTION)
                continue;

            TEST_ASSERT_FALSE_MESSAGE(strcmp(config_fields[i].name, config_fields[j].name) == 0,
                                      config_fields[i].name);
        }
    }
}

/* The header says [a-z0-9_] only, and three things depend on it: the POST
 * argument name, the MQTT topic segment, and the HTML the form is written
 * into. */
static void test_field_names_are_topic_and_url_safe(void)
{
    for (size_t i = 0; i < config_field_count; i++)
    {
        const struct config_field_s *f = &config_fields[i];

        if (f->kind == SETTINGS_SECTION)
            continue;

        for (const char *c = f->name; *c != '\0'; c++)
        {
            bool allowed = (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_';

            TEST_ASSERT_TRUE_MESSAGE(allowed, f->name);
        }
    }
}

/* ui_settings.cpp builds one page per tab and puts the rows of that tab on it.
 * A tab with no section at all would be an empty page with a Save button. */
static void test_every_tab_has_a_section(void)
{
    for (uint8_t tab = 0; tab < SETTINGS_TAB_COUNT; tab++)
    {
        bool found = false;

        for (size_t i = 0; i < config_field_count; i++)
            if (config_fields[i].kind == SETTINGS_SECTION && config_fields[i].tab == tab)
                found = true;

        /* The five tabs the table does not describe: WLAN is credentials, which are
         * not in Config, Info is a read-only table, and Fonts and Icons show
         * the typefaces and the icon set rather than any setting. */
        if (tab == SETTINGS_TAB_WLAN || tab == SETTINGS_TAB_INFO ||
            tab == SETTINGS_TAB_FONTS || tab == SETTINGS_TAB_ICONS)
            continue;

        TEST_ASSERT_TRUE_MESSAGE(found, "a settings tab with no rows");
    }
}

static void test_a_row_inherits_the_tab_of_its_section(void)
{
    /* The first row of the table is a section, so nothing can precede one. */
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SECTION, config_fields[0].kind);

    for (size_t i = 0; i < config_field_count; i++)
    {
        uint8_t expected = config_fields[i].kind == SETTINGS_SECTION
                               ? config_fields[i].tab
                               : config_field_tab(i - 1);

        TEST_ASSERT_EQUAL_UINT8(expected, config_field_tab(i));
    }
}

/* ------------------------------------------------- config_field_value_text */

static void test_value_text_renders_every_kind(void)
{
    char buffer[80];

    memset(&item, 0, sizeof(item));

    strcpy(item.mqtt.topic, "home/panels");
    item.mqtt.port = 1883;
    item.mqtt.retain = true;
    item.ui.theme = UI_THEME_LCARS;
    item.backlight.activity_timeout = 60;

    config_field_value_text(row("mqtt_topic"), &item, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_STRING("home/panels", buffer);

    config_field_value_text(row("mqtt_port"), &item, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_STRING("1883", buffer);

    /* ON and OFF, not 1 and 0: an openHAB Switch item takes the former, and so
     * does the panel's own row. */
    config_field_value_text(row("mqtt_retain"), &item, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_STRING("ON", buffer);

    item.mqtt.retain = false;
    config_field_value_text(row("mqtt_retain"), &item, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_STRING("OFF", buffer);

    config_field_value_text(row("theme"), &item, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_STRING(UI_THEME_NAME_LCARS, buffer);

    config_field_value_text(row("bl_timeout"), &item, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_STRING("60", buffer);

    /* A section has no value, and the MQTT client asks for one anyway while
     * walking the table. */
    config_field_value_text(&config_fields[0], &item, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_STRING("", buffer);
}

/* An out-of-range enum cannot come from the setters, but it can come from a
 * zero-initialised struct whose enum gained an entry, or from a config file
 * written by a newer firmware. */
static void test_value_text_survives_an_out_of_range_enum(void)
{
    char buffer[80];

    memset(&item, 0, sizeof(item));
    item.ui.theme = (enum ui_theme_family_e)99;

    config_field_value_text(row("theme"), &item, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_STRING(UI_THEME_NAME_MATERIAL, buffer);
}

/* Every non-secret row has to render into the MQTT client's value buffer, and
 * the client sizes that once for all of them. */
static void test_every_value_fits_a_publishable_buffer(void)
{
    char buffer[80];

    memset(&item, 0, sizeof(item));

    for (size_t i = 0; i < config_field_count; i++)
    {
        const struct config_field_s *f = &config_fields[i];

        if (f->kind == SETTINGS_TEXT)
            TEST_ASSERT_TRUE_MESSAGE(f->size <= sizeof(buffer), f->name);

        config_field_value_text(f, &item, buffer, sizeof(buffer));
    }
}

/* ------------------------------------------------------------- the setters */

static void test_hostchars_rows_reject_a_url(void)
{
    memset(&item, 0, sizeof(item));

    strcpy(item.mqtt.hostname, "mosquitto");

    /* What someone pastes when a field is labelled "Host". Rejected, and the
     * stored value left alone -- the browser's pattern attribute is not the
     * only thing enforcing this, because a hand-written POST does not run it. */
    TEST_ASSERT_FALSE(config_field_set_text(row("mqtt_host"), &item, "http://broker.lan"));
    TEST_ASSERT_EQUAL_STRING("mosquitto", item.mqtt.hostname);

    TEST_ASSERT_TRUE(config_field_set_text(row("mqtt_host"), &item, "broker.lan"));
    TEST_ASSERT_EQUAL_STRING("broker.lan", item.mqtt.hostname);
}

/* The base topic is the one text row that must accept '/': "home/panels" is
 * the reason it has no SETTINGS_F_HOSTCHARS. */
static void test_the_base_topic_accepts_a_path(void)
{
    memset(&item, 0, sizeof(item));

    TEST_ASSERT_TRUE(config_field_set_text(row("mqtt_topic"), &item, "home/panels"));
    TEST_ASSERT_EQUAL_STRING("home/panels", item.mqtt.topic);
}

static void test_numbers_are_clamped_not_rejected(void)
{
    const struct config_field_s *f = row("mqtt_port");

    memset(&item, 0, sizeof(item));

    config_field_set_number(f, &item, 0);
    TEST_ASSERT_EQUAL_INT32(f->min, config_field_read(f, &item));

    config_field_set_number(f, &item, 999999);
    TEST_ASSERT_EQUAL_INT32(f->max, config_field_read(f, &item));

    config_field_set_number(f, &item, 1883);
    TEST_ASSERT_EQUAL_INT32(1883, config_field_read(f, &item));
}

static void test_an_over_long_text_is_truncated(void)
{
    const struct config_field_s *f = row("mqtt_user");

    memset(&item, 0, sizeof(item));

    TEST_ASSERT_TRUE(config_field_set_text(f, &item, "0123456789012345678901234567890123456789"));
    TEST_ASSERT_EQUAL_UINT(f->size - 1, strlen(item.mqtt.user));
}

/* --------------------------------------------------------------- secrets */

static void test_the_broker_password_is_the_only_secret(void)
{
    size_t secrets = 0;

    for (size_t i = 0; i < config_field_count; i++)
    {
        if ((config_fields[i].flags & SETTINGS_F_SECRET) == 0)
            continue;

        secrets++;

        /* A secret is a string. The masking in ui_settings.cpp and the
         * password input in webui.cpp both only apply to SETTINGS_TEXT, so a
         * flagged number or checkbox would be flagged and still shown. */
        TEST_ASSERT_EQUAL_UINT8(SETTINGS_TEXT, config_fields[i].kind);
    }

    /* Not an arbitrary count: if a second secret appears, whoever added it
     * should confirm that both places that hide one still do. */
    TEST_ASSERT_EQUAL_UINT(1, secrets);
    TEST_ASSERT_TRUE((row("mqtt_pass")->flags & SETTINGS_F_SECRET) != 0);
}

/* ---------------------------------------------------------------- restart */

/* settings_restart_needed() is what makes the panel offer a reboot, so a field
 * that is in fact applied live must not be flagged: a flag on one of those
 * would ask for a restart after nearly every save. */
static void test_restart_is_needed_only_for_flagged_fields(void)
{
    config_item_t before;
    config_item_t after;
    const char   *label = NULL;

    memset(&before, 0, sizeof(before));
    memset(&after, 0, sizeof(after));

    TEST_ASSERT_FALSE(settings_restart_needed(&before, &after, &label));

    /* The MQTT settings are all live: the client restarts itself. */
    strcpy(after.mqtt.hostname, "broker.lan");
    after.mqtt.enabled = true;
    after.mqtt.port = 1883;
    TEST_ASSERT_FALSE(settings_restart_needed(&before, &after, &label));

    /* The hostname is not: it is handed to the WiFi driver during setup. */
    strcpy(after.general.hostname, "panel-hall");
    TEST_ASSERT_TRUE(settings_restart_needed(&before, &after, &label));
    TEST_ASSERT_EQUAL_STRING(row("hostname")->label, label);
}

/* ------------------------------------------------- the file half of a row */

/* The rows carry the config.json layout now, so a duplicated path would have
 * two settings overwrite each other in the file -- and only in the file, which
 * is the kind of bug that survives every interactive test. */
static void test_json_paths_are_unique(void)
{
    for (size_t i = 0; i < config_field_count; i++)
    {
        const struct config_field_s *a = &config_fields[i];

        if (a->kind == SETTINGS_SECTION)
            continue;

        TEST_ASSERT_NOT_NULL(a->json_path);
        TEST_ASSERT_NOT_NULL(a->json_key);
        TEST_ASSERT_TRUE(a->json_path[0] != '\0');
        TEST_ASSERT_TRUE(a->json_key[0] != '\0');

        /* config.cpp copies the path into a fixed buffer before walking it. */
        TEST_ASSERT_TRUE(strlen(a->json_path) < 32);

        /* '/' separates the objects of a path, so it cannot also appear in a
         * key -- "bme280/use" as a key would address nothing. */
        TEST_ASSERT_NULL(strchr(a->json_key, '/'));

        for (size_t j = i + 1; j < config_field_count; j++)
        {
            const struct config_field_s *b = &config_fields[j];

            if (b->kind == SETTINGS_SECTION)
                continue;

            if (strcmp(a->json_path, b->json_path) == 0)
                TEST_ASSERT_TRUE(strcmp(a->json_key, b->json_key) != 0);
        }
    }
}

/* A default outside its own row's range would be clamped the moment the file
 * was read, so the panel would boot with a value the table does not admit and
 * the next save would write a different one than the last load produced. */
static void test_every_default_is_in_range(void)
{
    config_item_t defaults;

    memset(&defaults, 0, sizeof(defaults));
    config_fields_set_defaults(&defaults);

    for (size_t i = 0; i < config_field_count; i++)
    {
        const struct config_field_s *f = &config_fields[i];

        switch (f->kind)
        {
        case SETTINGS_SECTION:
            break;

        case SETTINGS_TEXT:
            /* It has to survive the field it is copied into. */
            TEST_ASSERT_NOT_NULL(f->def_text);
            TEST_ASSERT_TRUE(strlen(f->def_text) < f->size);
            TEST_ASSERT_EQUAL_STRING(f->def_text, config_field_text(f, &defaults));
            break;

        case SETTINGS_ENUM:
        {
            /* By name, so the name has to be one the table knows: an unknown
             * one silently resolves to the first option. */
            char rendered[32];

            TEST_ASSERT_NOT_NULL(f->def_text);
            config_field_value_text(f, &defaults, rendered, sizeof(rendered));
            TEST_ASSERT_EQUAL_STRING(f->def_text, rendered);
            break;
        }

        default:
            TEST_ASSERT_TRUE(f->def_num >= f->min);
            TEST_ASSERT_TRUE(f->def_num <= f->max);
            TEST_ASSERT_EQUAL_INT32(f->def_num, config_field_read(f, &defaults));
            break;
        }
    }
}

/* The lookup the MQTT client and the simulator's environment overrides share. */
static void test_lookup_by_name(void)
{
    TEST_ASSERT_EQUAL_PTR(row("mqtt_host"), config_field_by_name("mqtt_host"));
    TEST_ASSERT_EQUAL_PTR(row("theme"), config_field_by_name("theme"));

    TEST_ASSERT_NULL(config_field_by_name("no_such_setting"));
    TEST_ASSERT_NULL(config_field_by_name(""));
    TEST_ASSERT_NULL(config_field_by_name(NULL));

    /* A section has a label but no name, and must never be returned: the
     * accessors would read a zero offset as though it were a field. */
    TEST_ASSERT_NULL(config_field_by_name("Device"));
    TEST_ASSERT_NULL(config_field_by_name("MQTT Broker"));
}

void test_config_fields_run(void)
{
    RUN_TEST(test_every_row_is_well_formed);
    RUN_TEST(test_field_names_are_unique);
    RUN_TEST(test_field_names_are_topic_and_url_safe);
    RUN_TEST(test_every_tab_has_a_section);
    RUN_TEST(test_a_row_inherits_the_tab_of_its_section);
    RUN_TEST(test_value_text_renders_every_kind);
    RUN_TEST(test_value_text_survives_an_out_of_range_enum);
    RUN_TEST(test_every_value_fits_a_publishable_buffer);
    RUN_TEST(test_hostchars_rows_reject_a_url);
    RUN_TEST(test_the_base_topic_accepts_a_path);
    RUN_TEST(test_numbers_are_clamped_not_rejected);
    RUN_TEST(test_an_over_long_text_is_truncated);
    RUN_TEST(test_the_broker_password_is_the_only_secret);
    RUN_TEST(test_restart_is_needed_only_for_flagged_fields);
    RUN_TEST(test_json_paths_are_unique);
    RUN_TEST(test_every_default_is_in_range);
    RUN_TEST(test_lookup_by_name);
}
