#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <Arduino.h>
#include <ArduinoJson.h>
#include <stdlib.h>
#include "debug.h"

#include "esp_log.h"
#include "ui_theme.hpp"
#if (SIMULATOR == 0)
#include "SPIFFS.h"
#endif

#ifndef DEBUG_CONFIG
#define DEBUG_CONFIG 0
#endif

/* The largest config file that will be read. Sized well above what the file
 * actually needs -- the shipped data/config.json is around 900 bytes of
 * pretty-printed JSON, and saveConfig() writes it back compacted to about half
 * that -- so that adding a setting does not silently push the file over the
 * limit and fall back to every default. It is still bounded, because the size
 * comes from the filesystem and a corrupt SPIFFS can report anything; the
 * buffer below is allocated from the real file size, not from this. */
#ifndef CONFIG_FILE_MAX_SIZE
#define CONFIG_FILE_MAX_SIZE 2048
#endif

class Config
{
private:
    String config_filename = "";

public:
    struct
    {
        struct
        {
            char hostname[32];
        } general;
        struct
        {
            char hostname[32];
            int gmt_offset;
            bool daylightsaving;
        } ntp;
        struct
        {
            /* Stored by name in the file, as an enum here: an unknown name
             * cannot survive loadConfig(), so every consumer -- the styles, the
             * web form's dropdown -- gets a value that is valid by
             * construction. See ui_theme.hpp. */
            enum ui_theme_family_e theme;
            enum ui_night_mode_e night_mode;
            unsigned int night_from;
            unsigned int night_to;
        } ui;
        struct
        {
            unsigned long activity_timeout;
            unsigned int normal_brightness;
            unsigned int dim_brightness;
        } backlight;
        struct
        {
            bool enabled;
        } beeper;
        struct
        {
            char hostname[32];
            int port;
            char sitemap[32];
            struct
            {
                struct
                {
                    bool use;
                    int interval;
                    struct
                    {
                        char temperature[32];
                        char humidity[32];
                        char pressure[32];
                    } items;
                } bme280;
            } sensors;
        } openhab;
    } item;

    bool setup()
    {
#if (SIMULATOR != 1)
        if (!SPIFFS.begin())
        {
            ESP_LOGE("config", "Failed to mount file system");
            return false;
        }
        return true;
#else
    return false;
#endif
    }

    bool loadConfig(String filename)
    {
#if (SIMULATOR != 1)
        config_filename = filename;

        debug_printf("loadConfig file: %s\r\n", filename.c_str());

        File configFile = SPIFFS.open(filename, "r");
        if (!configFile)
        {
            debug_printf("Failed to open config file");
            return false;
        }

        size_t size = configFile.size();
        if (size > CONFIG_FILE_MAX_SIZE)
        {
            configFile.close();
            /* Say by how much: a file that grew past the limit reads as "every
             * setting reverted to its default", which is otherwise a puzzle. */
            ESP_LOGE("config", "Config file size %u is too large (max %u)",
                     (unsigned)size, (unsigned)CONFIG_FILE_MAX_SIZE);
            return false;
        }

        std::unique_ptr<char[]> buf(new char[size]);

        size_t read_size = configFile.readBytes(buf.get(), size);

        configFile.close();

        JsonDocument doc;
        /* readBytes() does not terminate the buffer, so the parser has to be
         * given its length -- the const char * overload would read past the
         * end. The char * overload also parses in place, without copying. */
        auto error = deserializeJson(doc, buf.get(), read_size);
        if (error)
        {
            ESP_LOGE("config", "Failed to parse config file");
            return false;
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
        strlcpy(item.openhab.hostname, doc["openhab"]["hostname"] | "openhabian", sizeof(item.openhab.hostname));
        item.openhab.port = doc["openhab"]["port"] | 8080;
        strlcpy(item.openhab.sitemap, doc["openhab"]["sitemap"] | "setme_sitemap", sizeof(item.openhab.sitemap));
        item.openhab.sensors.bme280.use = doc["openhab"]["sensors"]["bme280"]["use"] | false;
        item.openhab.sensors.bme280.interval = doc["openhab"]["sensors"]["bme280"]["interval"] | 180;
        strlcpy(item.openhab.sensors.bme280.items.temperature, doc["openhab"]["sensors"]["bme280"]["items"]["temperature"] | "", sizeof(item.openhab.sensors.bme280.items.temperature));
        strlcpy(item.openhab.sensors.bme280.items.humidity, doc["openhab"]["sensors"]["bme280"]["items"]["humidity"] | "", sizeof(item.openhab.sensors.bme280.items.humidity));
        strlcpy(item.openhab.sensors.bme280.items.pressure, doc["openhab"]["sensors"]["bme280"]["items"]["pressure"] | "", sizeof(item.openhab.sensors.bme280.items.pressure));
#if DEBUG_CONFIG
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
        debug_printf("  item.openhab.hostname: %s\r\n", item.openhab.hostname);
        debug_printf("  item.openhab.port: %d\r\n", item.openhab.port);
        debug_printf("  item.openhab.sitemap: %s\r\n", item.openhab.sitemap);
        debug_printf("  item.openhab.sensors.bme280.use: %d\r\n", item.openhab.sensors.bme280.use);
        debug_printf("  item.openhab.sensors.bme280.interval: %d\r\n", item.openhab.sensors.bme280.interval);
        debug_printf("  item.openhab.sensors.bme280.items.temperature: %s\r\n", item.openhab.sensors.bme280.items.temperature);
        debug_printf("  item.openhab.sensors.bme280.items.humidity: %s\r\n", item.openhab.sensors.bme280.items.humidity);
        debug_printf("  item.openhab.sensors.bme280.items.pressure: %s\r\n", item.openhab.sensors.bme280.items.pressure);
#endif
        return true;
#else /* #if (SIMULATOR != 1) */
        // There is no file system in the simulator; use the values the canned
        // sitemap in src/sim/sitemap_fixture.cpp is served under.
        (void)filename;

        strlcpy(item.general.hostname, "oheztouch-sim", sizeof(item.general.hostname));

        strlcpy(item.ntp.hostname, "pool.ntp.org", sizeof(item.ntp.hostname));
        item.ntp.gmt_offset = 1;
        item.ntp.daylightsaving = true;

        /* No filesystem and no web server on the host, so the theme comes from
         * the environment -- which means all six variants can be compared
         * without a rebuild:
         *   OHEZ_THEME=lcars OHEZ_NIGHT=on pio run -e linux -t exec
         * getenv() returns NULL when unset, and the ui_theme.hpp lookups read
         * that as the first entry. */
        item.ui.theme = ui_theme_from_name(getenv("OHEZ_THEME"));
        item.ui.night_mode = ui_night_mode_from_name(getenv("OHEZ_NIGHT"));
        /* Without these two the "auto" mode would be stuck on the 22-6 window
         * and could only be watched by waiting for it. atoi() answers 0 for
         * unset and for nonsense alike, which is a valid hour. */
        item.ui.night_from = getenv("OHEZ_NIGHT_FROM") ? (unsigned int)atoi(getenv("OHEZ_NIGHT_FROM")) : 22;
        item.ui.night_to = getenv("OHEZ_NIGHT_TO") ? (unsigned int)atoi(getenv("OHEZ_NIGHT_TO")) : 6;

        item.backlight.activity_timeout = 0;
        item.backlight.normal_brightness = 100;
        item.backlight.dim_brightness = 10;

        item.beeper.enabled = false;

        strlcpy(item.openhab.hostname, "localhost", sizeof(item.openhab.hostname));
        item.openhab.port = 8080;
        strlcpy(item.openhab.sitemap, "demo", sizeof(item.openhab.sitemap));

        item.openhab.sensors.bme280.use = false;

        return true;
#endif
    }

    bool saveConfig()
    {
#if (SIMULATOR != 1)
        if (config_filename.isEmpty())
            return false;

        debug_printf("saveConfig file: %s\r\n", config_filename.c_str());

        File configFile = SPIFFS.open(config_filename, "w");
        if (!configFile)
        {
            ESP_LOGE("config", "Failed to open config file for writing");
            return false;
        }

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

        doc["openhab"]["hostname"] = item.openhab.hostname;
        doc["openhab"]["port"] = item.openhab.port;
        doc["openhab"]["sitemap"] = item.openhab.sitemap;

        doc["openhab"]["sensors"]["bme280"]["use"] = item.openhab.sensors.bme280.use;
        doc["openhab"]["sensors"]["bme280"]["interval"] = item.openhab.sensors.bme280.interval;
        doc["openhab"]["sensors"]["bme280"]["items"]["temperature"] = item.openhab.sensors.bme280.items.temperature;
        doc["openhab"]["sensors"]["bme280"]["items"]["humidity"] = item.openhab.sensors.bme280.items.humidity;
        doc["openhab"]["sensors"]["bme280"]["items"]["pressure"] = item.openhab.sensors.bme280.items.pressure;

        serializeJson(doc, configFile);
        configFile.close();
        return true;
#else /* #if (SIMULATOR != 1) */
        return false;
#endif
    }
};

#endif
