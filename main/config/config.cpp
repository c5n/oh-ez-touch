/**
 * @file config.cpp
 *
 * See config.hpp. This used to be a header-only class with two
 * implementations of loadConfig() selected by `#if (SIMULATOR)`; it is one
 * implementation over port_storage now, which is why it needs a .cpp at all.
 *
 * Nothing here names a setting. loadConfig(), saveConfig(), the debug dump and
 * the simulator's environment overrides are all one pass over config_fields[],
 * which carries each row's JSON path and its default -- so a setting added to
 * that table is stored, restored and dumped without this file changing. It
 * used to hold two more hand-written copies of the field list, and they were
 * the two that a new setting was easiest to forget.
 */

#include "config.hpp"

#include "config_fields.hpp"
#include "debug.h"
#include "port/port_storage.h"

#include <ArduinoJson.h>
#include <memory>
#include <stdlib.h>
#include <string.h>
#include <strings.h> /* strcasecmp(): OHEZ_MQTT takes a word, not a number */

#include "esp_log.h"

static const char *TAG = "config";

/* The longest json_path in the table, plus room. strtok_r() writes into its
 * subject, so the path has to be copied before it is walked. */
#define CONFIG_JSON_PATH_MAX 32

void Config::lock()
{
    if (mutex != NULL)
        xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
}

void Config::unlock()
{
    if (mutex != NULL)
        xSemaphoreGiveRecursive(mutex);
}

bool Config::setup()
{
    if (mutex == NULL)
        mutex = xSemaphoreCreateRecursiveMutex();

    esp_err_t err = port_storage_init();

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "config store unavailable: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}

/* ------------------------------------------------------------- JSON paths */

/* The value of one field, or a null variant when the file does not carry it.
 *
 * Walking a null variant is safe in ArduinoJson -- every step of a missing
 * path yields null again -- so a file missing a whole section needs no special
 * case here; the caller simply keeps the default. */
static JsonVariantConst config_json_read(JsonVariantConst root,
                                         const struct config_field_s *f)
{
    char  path[CONFIG_JSON_PATH_MAX];
    char *save = NULL;

    strlcpy(path, f->json_path, sizeof(path));

    JsonVariantConst node = root;

    for (char *seg = strtok_r(path, "/", &save); seg != NULL;
         seg = strtok_r(NULL, "/", &save))
        node = node[seg];

    return node[f->json_key];
}

/* The object a field is written into, created if it is not there yet. */
static JsonObject config_json_object(JsonObject root, const struct config_field_s *f)
{
    char  path[CONFIG_JSON_PATH_MAX];
    char *save = NULL;

    strlcpy(path, f->json_path, sizeof(path));

    JsonObject node = root;

    for (char *seg = strtok_r(path, "/", &save); seg != NULL;
         seg = strtok_r(NULL, "/", &save))
    {
        JsonObject child = node[seg].as<JsonObject>();

        if (child.isNull())
            child = node[seg].to<JsonObject>();

        node = child;
    }

    return node;
}

/* ----------------------------------------------------------- env overrides */

/* The OHEZ_* overrides. An explicit debug convenience, applied after the file so
 * that it can be inspected without being edited -- all six theme variants can be
 * compared without a rebuild, and the "auto" night mode can be watched without
 * waiting for 22:00:
 *
 *   OHEZ_THEME=lcars OHEZ_NIGHT=auto OHEZ_NIGHT_FROM=8 ./build/linux/oh-ez-touch.elf
 *
 * and, since the simulator makes real requests now, which openHAB to talk to:
 *
 *   OHEZ_OPENHAB_HOST=openhab.lan OHEZ_SITEMAP=oheztouch ./build/linux/oh-ez-touch.elf
 *
 * and, for the same reason, which broker to publish to:
 *
 *   OHEZ_MQTT=on OHEZ_MQTT_HOST=localhost ./build/linux/oh-ez-touch.elf
 *
 * They apply on both targets, not just the simulator: the device has no
 * environment to read, so the calls are inert there rather than guarded. Only a
 * variable that is actually set overrides the file.
 *
 * The variable names are spelled out rather than derived from the field names,
 * because they are documented in README.md and used in scripts: OHEZ_SITEMAP
 * is not what "oh_sitemap" would generate. Adding one is a line here.
 */
static const struct
{
    const char *env;
    const char *field;
} config_env_overrides[] = {
    {"OHEZ_THEME", "theme"},
    {"OHEZ_NIGHT", "night_mode"},
    {"OHEZ_NIGHT_FROM", "night_from"},
    {"OHEZ_NIGHT_TO", "night_to"},
    {"OHEZ_OPENHAB_HOST", "oh_host"},
    {"OHEZ_OPENHAB_PORT", "oh_port"},
    {"OHEZ_SITEMAP", "oh_sitemap"},
    {"OHEZ_MQTT", "mqtt_use"},
    {"OHEZ_MQTT_HOST", "mqtt_host"},
    {"OHEZ_MQTT_PORT", "mqtt_port"},
    {"OHEZ_MQTT_TOPIC", "mqtt_topic"},
};

static void config_apply_env_overrides(config_item_t &item)
{
    for (size_t i = 0; i < sizeof(config_env_overrides) / sizeof(config_env_overrides[0]); i++)
    {
        const char *value = getenv(config_env_overrides[i].env);

        if (value == NULL)
            continue;

        const struct config_field_s *f = config_field_by_name(config_env_overrides[i].field);

        if (f == NULL)
        {
            /* A row renamed out from under the list above. Worth saying: the
             * variable would otherwise be silently ignored. */
            ESP_LOGW(TAG, "%s names no setting (%s)", config_env_overrides[i].env,
                     config_env_overrides[i].field);
            continue;
        }

        switch (f->kind)
        {
        case SETTINGS_TEXT:
            config_field_set_text(f, &item, value);
            break;

        case SETTINGS_ENUM:
            /* An unknown name selects the first option rather than nothing,
             * so a typo in OHEZ_THEME gives Material rather than no theme at
             * all. */
            config_field_write(f, &item, config_field_enum_from_name(f, value));
            break;

        case SETTINGS_BOOL:
            /* Anything but "off" or "0" enables it, so OHEZ_MQTT=1, =on and
             * =yes all work; the point of the variable is to switch something
             * on for one run, not to be a second configuration language. */
            config_field_write(f, &item,
                               (strcasecmp(value, "off") != 0 && strcmp(value, "0") != 0));
            break;

        default:
            config_field_set_number(f, &item, strtol(value, NULL, 10));
            break;
        }

        ESP_LOGI(TAG, "%s overrides %s", config_env_overrides[i].env, f->name);
    }
}

/* ------------------------------------------------------------------- load */

/* Apply one field from the parsed document, or leave the default in place.
 *
 * Every value goes through the same setters the web form and the settings
 * screen use, so a hand-edited file cannot put a value into Config that
 * neither front end would accept: a number outside the row's range is clamped,
 * and a hostname containing '/' or ':' is refused and the default stands. That
 * is new -- the old hand-written loader assigned whatever the file said.
 */
static void config_load_field(config_item_t &item, JsonVariantConst root,
                              const struct config_field_s *f)
{
    JsonVariantConst value = config_json_read(root, f);

    if (value.isNull())
        return;

    switch (f->kind)
    {
    case SETTINGS_TEXT:
    {
        const char *text = value.as<const char *>();

        if (text != NULL)
            config_field_set_text(f, &item, text);
        break;
    }

    case SETTINGS_BOOL:
        config_field_write(f, &item, value.as<bool>() ? 1 : 0);
        break;

    case SETTINGS_ENUM:
        config_field_write(f, &item, config_field_enum_from_name(f, value.as<const char *>()));
        break;

    default:
        config_field_set_number(f, &item, value.as<long>());
        break;
    }
}

#if CONFIG_OHEZ_DEBUG_CONFIG_FILE
/* Every setting as it ended up, secrets excluded. One loop rather than the
 * thirty printf() lines this used to be, which is also why it can no longer
 * fall behind the table. */
static void config_dump(const config_item_t &item)
{
    printf("Config::loadConfig: loaded values\r\n");

    for (size_t i = 0; i < config_field_count; i++)
    {
        const struct config_field_s *f = &config_fields[i];
        char                         value[64];

        if (f->kind == SETTINGS_SECTION)
            continue;

        /* The password is deliberately not printed. Everything else in this
         * dump is already visible in the web form; that one is not. */
        if (f->flags & SETTINGS_F_SECRET)
        {
            printf("  %-14s %s/%s = *****\r\n", f->name, f->json_path, f->json_key);
            continue;
        }

        config_field_value_text(f, &item, value, sizeof(value));
        printf("  %-14s %s/%s = %s\r\n", f->name, f->json_path, f->json_key, value);
    }
}
#endif

bool Config::loadConfig(const char *name)
{
    strlcpy(config_filename, name, sizeof(config_filename));

    debug_printf("loadConfig file: %s\r\n", config_filename);

    /* Parsed on success, left empty on every failure path below. The defaults
     * go in first either way, so a missing, oversized or unparseable file
     * leaves a complete set of settings rather than whatever was in memory. */
    JsonDocument doc;
    bool         from_file = false;

    config_fields_set_defaults(&item);

    ssize_t size = port_storage_size(name);

    if (size < 0)
    {
        /* Not an error: a device whose filesystem has just been written, or a
         * fresh simulator, has no file yet. The defaults are a working
         * configuration and the first saveConfig() creates the file. */
        ESP_LOGI(TAG, "no %s yet, using defaults", name);
    }
    else if (size > CONFIG_FILE_MAX_SIZE)
    {
        /* Say by how much: a file that grew past the limit reads as "every
         * setting reverted to its default", which is otherwise a puzzle. */
        ESP_LOGE(TAG, "Config file size %u is too large (max %u)",
                 (unsigned)size, (unsigned)CONFIG_FILE_MAX_SIZE);
    }
    else
    {
        std::unique_ptr<char[]> buf(new char[size]);
        ssize_t read_size = port_storage_read(name, buf.get(), (size_t)size);

        if (read_size < 0)
        {
            ESP_LOGE(TAG, "Failed to read config file");
        }
        else
        {
            /* The buffer is not terminated, so the parser has to be given its
             * length -- the const char * overload would read past the end. The
             * char * overload also parses in place, without copying. */
            DeserializationError error = deserializeJson(doc, buf.get(), (size_t)read_size);

            if (error)
            {
                ESP_LOGE(TAG, "Failed to parse config file: %s", error.c_str());
                /* deserializeJson() may leave a partial document behind, and a
                 * half-parsed file is worse than none: it would mix values from
                 * the file with defaults, unpredictably. */
                doc.clear();
            }
            else
            {
                from_file = true;
            }
        }
    }

    JsonVariantConst root = doc.as<JsonVariantConst>();

    for (size_t i = 0; i < config_field_count; i++)
        if (config_fields[i].kind != SETTINGS_SECTION)
            config_load_field(item, root, &config_fields[i]);

    config_apply_env_overrides(item);

#if CONFIG_OHEZ_DEBUG_CONFIG_FILE
    config_dump(item);
#endif

    return from_file;
}

/* ------------------------------------------------------------------- save */

bool Config::saveConfig()
{
    if (config_filename[0] == '\0')
        return false;

    debug_printf("saveConfig file: %s\r\n", config_filename);

    JsonDocument doc;
    JsonObject   root = doc.to<JsonObject>();

    for (size_t i = 0; i < config_field_count; i++)
    {
        const struct config_field_s *f = &config_fields[i];

        if (f->kind == SETTINGS_SECTION)
            continue;

        JsonObject obj = config_json_object(root, f);

        switch (f->kind)
        {
        case SETTINGS_TEXT:
            obj[f->json_key] = config_field_text(f, &item);
            break;

        case SETTINGS_BOOL:
            /* As a JSON boolean, not as 0 or 1: the file is meant to be
             * readable and hand-editable. */
            obj[f->json_key] = (config_field_read(f, &item) != 0);
            break;

        case SETTINGS_ENUM:
        {
            /* By name, and through the same bounds check every other reader
             * of an enum uses: an index out of range would otherwise be an
             * out-of-bounds read of ->names on the way to the file. */
            char option[32];

            config_field_value_text(f, &item, option, sizeof(option));
            obj[f->json_key] = option;
            break;
        }

        default:
            obj[f->json_key] = (long)config_field_read(f, &item);
            break;
        }
    }

    /* port_storage replaces a blob whole, so the document is serialized into
     * memory first rather than streamed into an open File as it used to be.
     * measureJson() is exact, and the +1 is the terminator serializeJson()
     * writes but does not count. */
    size_t json_size = measureJson(doc);
    std::unique_ptr<char[]> buf(new char[json_size + 1]);

    size_t written = serializeJson(doc, buf.get(), json_size + 1);

    if (written != json_size)
    {
        ESP_LOGE(TAG, "serialized %u bytes, expected %u",
                 (unsigned)written, (unsigned)json_size);
        return false;
    }

    if (port_storage_write(config_filename, buf.get(), written) != (ssize_t)written)
    {
        ESP_LOGE(TAG, "Failed to write config file");
        return false;
    }

    ESP_LOGD(TAG, "saved %u bytes to %s", (unsigned)written, config_filename);

    return true;
}
