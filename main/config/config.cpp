/**
 * @file config.cpp
 *
 * See config.hpp. This used to be a header-only class with two
 * implementations of loadConfig() selected by `#if (SIMULATOR)`; it is one
 * implementation over port_storage now, which is why it needs a .cpp at all.
 */

#include "config.hpp"

#include "debug.h"
#include "port/port_storage.h"

#include <ArduinoJson.h>
#include <memory>
#include <stdlib.h>
#include <string.h>
#include <strings.h> /* strcasecmp(): OHEZ_MQTT takes a word, not a number */

#include "esp_log.h"

static const char *TAG = "config";

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
 * variable that is actually set overrides the file. */
static void config_apply_env_overrides(decltype(Config::item) &item)
{
    const char *theme      = getenv("OHEZ_THEME");
    const char *night      = getenv("OHEZ_NIGHT");
    const char *from       = getenv("OHEZ_NIGHT_FROM");
    const char *to         = getenv("OHEZ_NIGHT_TO");
    const char *host       = getenv("OHEZ_OPENHAB_HOST");
    const char *port       = getenv("OHEZ_OPENHAB_PORT");
    const char *sitemap    = getenv("OHEZ_SITEMAP");
    const char *mqtt       = getenv("OHEZ_MQTT");
    const char *mqtt_host  = getenv("OHEZ_MQTT_HOST");
    const char *mqtt_port  = getenv("OHEZ_MQTT_PORT");
    const char *mqtt_topic = getenv("OHEZ_MQTT_TOPIC");

    /* ui_theme_from_name() falls back to the first entry for a name it does not
     * know, so a typo here selects the default theme rather than nothing. */
    if (theme != NULL)
        item.ui.theme = ui_theme_from_name(theme);

    if (night != NULL)
        item.ui.night_mode = ui_night_mode_from_name(night);

    if (from != NULL)
        item.ui.night_from = (unsigned int)atoi(from);

    if (to != NULL)
        item.ui.night_to = (unsigned int)atoi(to);

    if (host != NULL)
        strlcpy(item.openhab.hostname, host, sizeof(item.openhab.hostname));

    if (port != NULL)
        item.openhab.port = atoi(port);

    if (sitemap != NULL)
        strlcpy(item.openhab.sitemap, sitemap, sizeof(item.openhab.sitemap));

    /* Anything but "off" or "0" enables it, so OHEZ_MQTT=1, =on and =yes all
     * work; the point of the variable is to switch the client on for one run
     * without editing the file, not to be a second configuration language. */
    if (mqtt != NULL)
        item.mqtt.enabled = (strcasecmp(mqtt, "off") != 0 && strcmp(mqtt, "0") != 0);

    if (mqtt_host != NULL)
        strlcpy(item.mqtt.hostname, mqtt_host, sizeof(item.mqtt.hostname));

    if (mqtt_port != NULL)
        item.mqtt.port = atoi(mqtt_port);

    if (mqtt_topic != NULL)
        strlcpy(item.mqtt.topic, mqtt_topic, sizeof(item.mqtt.topic));
}

bool Config::loadConfig(const char *name)
{
    strlcpy(config_filename, name, sizeof(config_filename));

    debug_printf("loadConfig file: %s\r\n", config_filename);

    /* Parsed on success, left empty on every failure path below. An empty
     * document makes each `| default` the effective value, so the field
     * assignments further down are the one place the defaults live and they run
     * whether or not there is a file to read. */
    JsonDocument doc;
    bool from_file = false;

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
                ESP_LOGE(TAG, "Failed to parse config file");
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

    strlcpy(item.general.hostname, doc["general"]["hostname"] | "oheztouch-new", sizeof(item.general.hostname));
    strlcpy(item.ntp.hostname, doc["ntp"]["hostname"] | "pool.ntp.org", sizeof(item.ntp.hostname));
    item.ntp.gmt_offset = doc["ntp"]["gmt_offset"] | 1;
    item.ntp.daylightsaving = doc["ntp"]["daylightsaving"] | false;
    item.ui.theme = ui_theme_from_name(doc["ui"]["theme"] | UI_THEME_NAME_DEFAULT);
    item.ui.night_mode = ui_night_mode_from_name(doc["ui"]["night_mode"] | UI_NIGHT_NAME_OFF);
    item.ui.night_from = doc["ui"]["night_from"] | 22;
    item.ui.night_to = doc["ui"]["night_to"] | 6;
    item.backlight.activity_timeout = doc["backlight"]["activity_timeout"] | 60;
    item.backlight.normal_brightness = doc["backlight"]["normal_brightness"] | 100;
    item.backlight.dim_brightness = doc["backlight"]["dim_brightness"] | 40;
    item.beeper.enabled = doc["beeper"]["enabled"] | true;
    item.mqtt.enabled = doc["mqtt"]["enabled"] | false;
    strlcpy(item.mqtt.hostname, doc["mqtt"]["hostname"] | "mosquitto", sizeof(item.mqtt.hostname));
    item.mqtt.port = doc["mqtt"]["port"] | 1883;
    strlcpy(item.mqtt.user, doc["mqtt"]["user"] | "", sizeof(item.mqtt.user));
    strlcpy(item.mqtt.password, doc["mqtt"]["password"] | "", sizeof(item.mqtt.password));
    strlcpy(item.mqtt.topic, doc["mqtt"]["topic"] | "oheztouch", sizeof(item.mqtt.topic));
    item.mqtt.interval = doc["mqtt"]["interval"] | 60;
    item.mqtt.retain = doc["mqtt"]["retain"] | true;
    item.ble.enabled = doc["ble"]["enabled"] | false;
    item.ble.interval = doc["ble"]["interval"] | 30;
    item.ble.window = doc["ble"]["window"] | 5;
    item.ble.rssi_min = doc["ble"]["rssi_min"] | -90;
    item.ble.expire = doc["ble"]["expire"] | 120;
    item.ble.publish_all = doc["ble"]["publish_all"] | false;
    strlcpy(item.openhab.hostname, doc["openhab"]["hostname"] | "openhabian", sizeof(item.openhab.hostname));
    item.openhab.port = doc["openhab"]["port"] | 8080;
    strlcpy(item.openhab.sitemap, doc["openhab"]["sitemap"] | "setme_sitemap", sizeof(item.openhab.sitemap));
    item.sensors.bme280.use = doc["sensors"]["bme280"]["use"] | false;
    item.sensors.bme280.interval = doc["sensors"]["bme280"]["interval"] | 180;

    config_apply_env_overrides(item);

#if CONFIG_OHEZ_DEBUG_CONFIG_FILE
    printf("Config::loadConfig: Loaded Values\r\n");
    debug_printf("  item.general.hostname: %s\r\n", item.general.hostname);
    debug_printf("  item.ntp.hostname: %s\r\n", item.ntp.hostname);
    debug_printf("  item.ntp.gmt_offset: %d\r\n", item.ntp.gmt_offset);
    debug_printf("  item.ntp.daylightsaving: %d\r\n", item.ntp.daylightsaving);
    debug_printf("  item.ui.theme: %d (%s)\r\n", (int)item.ui.theme, ui_theme_name(item.ui.theme));
    debug_printf("  item.ui.night_mode: %d (%s)\r\n", (int)item.ui.night_mode, ui_night_mode_name(item.ui.night_mode));
    debug_printf("  item.ui.night_from: %u\r\n", item.ui.night_from);
    debug_printf("  item.ui.night_to: %u\r\n", item.ui.night_to);
    debug_printf("  item.backlight.activity_timeout: %lu\r\n", item.backlight.activity_timeout);
    debug_printf("  item.backlight.normal_brightness: %u\r\n", item.backlight.normal_brightness);
    debug_printf("  item.backlight.dim_brightness: %u\r\n", item.backlight.dim_brightness);
    debug_printf("  item.beeper.enabled: %d\r\n", item.beeper.enabled);
    debug_printf("  item.mqtt.enabled: %d\r\n", item.mqtt.enabled);
    debug_printf("  item.mqtt.hostname: %s\r\n", item.mqtt.hostname);
    debug_printf("  item.mqtt.port: %d\r\n", item.mqtt.port);
    debug_printf("  item.mqtt.user: %s\r\n", item.mqtt.user);
    /* The password is deliberately not printed. Everything else in this dump
     * is already visible in the web form; that one is not. */
    debug_printf("  item.mqtt.topic: %s\r\n", item.mqtt.topic);
    debug_printf("  item.mqtt.interval: %d\r\n", item.mqtt.interval);
    debug_printf("  item.mqtt.retain: %d\r\n", item.mqtt.retain);
    debug_printf("  item.ble.enabled: %d\r\n", item.ble.enabled);
    debug_printf("  item.ble.interval: %d\r\n", item.ble.interval);
    debug_printf("  item.ble.window: %d\r\n", item.ble.window);
    debug_printf("  item.ble.rssi_min: %d\r\n", item.ble.rssi_min);
    debug_printf("  item.ble.expire: %d\r\n", item.ble.expire);
    debug_printf("  item.ble.publish_all: %d\r\n", item.ble.publish_all);
    debug_printf("  item.openhab.hostname: %s\r\n", item.openhab.hostname);
    debug_printf("  item.openhab.port: %d\r\n", item.openhab.port);
    debug_printf("  item.openhab.sitemap: %s\r\n", item.openhab.sitemap);
    debug_printf("  item.sensors.bme280.use: %d\r\n", item.sensors.bme280.use);
    debug_printf("  item.sensors.bme280.interval: %d\r\n", item.sensors.bme280.interval);
#endif

    return from_file;
}

bool Config::saveConfig()
{
    if (config_filename[0] == '\0')
        return false;

    debug_printf("saveConfig file: %s\r\n", config_filename);

    JsonDocument doc;

    doc["general"]["hostname"] = item.general.hostname;

    doc["ntp"]["hostname"] = item.ntp.hostname;
    doc["ntp"]["gmt_offset"] = item.ntp.gmt_offset;
    doc["ntp"]["daylightsaving"] = item.ntp.daylightsaving;

    doc["ui"]["theme"] = ui_theme_name(item.ui.theme);
    doc["ui"]["night_mode"] = ui_night_mode_name(item.ui.night_mode);
    doc["ui"]["night_from"] = item.ui.night_from;
    doc["ui"]["night_to"] = item.ui.night_to;

    doc["backlight"]["activity_timeout"] = item.backlight.activity_timeout;
    doc["backlight"]["normal_brightness"] = item.backlight.normal_brightness;
    doc["backlight"]["dim_brightness"] = item.backlight.dim_brightness;

    doc["beeper"]["enabled"] = item.beeper.enabled;

    doc["mqtt"]["enabled"] = item.mqtt.enabled;
    doc["mqtt"]["hostname"] = item.mqtt.hostname;
    doc["mqtt"]["port"] = item.mqtt.port;
    doc["mqtt"]["user"] = item.mqtt.user;
    doc["mqtt"]["password"] = item.mqtt.password;
    doc["mqtt"]["topic"] = item.mqtt.topic;
    doc["mqtt"]["interval"] = item.mqtt.interval;
    doc["mqtt"]["retain"] = item.mqtt.retain;

    doc["ble"]["enabled"] = item.ble.enabled;
    doc["ble"]["interval"] = item.ble.interval;
    doc["ble"]["window"] = item.ble.window;
    doc["ble"]["rssi_min"] = item.ble.rssi_min;
    doc["ble"]["expire"] = item.ble.expire;
    doc["ble"]["publish_all"] = item.ble.publish_all;

    doc["openhab"]["hostname"] = item.openhab.hostname;
    doc["openhab"]["port"] = item.openhab.port;
    doc["openhab"]["sitemap"] = item.openhab.sitemap;

    doc["sensors"]["bme280"]["use"] = item.sensors.bme280.use;
    doc["sensors"]["bme280"]["interval"] = item.sensors.bme280.interval;

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

    return true;
}
