#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <ArduinoJson.h>
#include "debug.h"
#if (SIMULATOR == 0)
#include "SPIFFS.h"
#endif

#ifndef DEBUG_CONFIG
#define DEBUG_CONFIG 0
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
            Serial.println("Failed to mount file system");
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
        if (size > 1024)
        {
            configFile.close();
            Serial.println("Config file size is too large");
            return false;
        }

        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);

        configFile.close();

        StaticJsonDocument<512> doc;
        auto error = deserializeJson(doc, buf.get());
        if (error)
        {
            Serial.println("Failed to parse config file");
            return false;
        }

        strlcpy(item.general.hostname, doc["general"]["hostname"] | "oheztouch-new", sizeof(item.general.hostname));
        strlcpy(item.ntp.hostname, doc["ntp"]["hostname"] | "pool.ntp.org", sizeof(item.ntp.hostname));
        item.ntp.gmt_offset = doc["ntp"]["gmt_offset"] | 1;
        item.ntp.daylightsaving = doc["ntp"]["daylightsaving"] | false;
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
        Serial.println("Config::loadConfig: Loaded Values");
        debug_printf("  item.general.hostname: %s\r\n", item.general.hostname);
        debug_printf("  item.ntp.hostname: %s\r\n", item.ntp.hostname);
        debug_printf("  item.ntp.gmt_offset: %d\r\n", item.ntp.gmt_offset);
        debug_printf("  item.ntp.daylightsaving: %d\r\n", item.ntp.daylightsaving);
        debug_printf("  item.backlight.activity_timeout: %s\r\n", item.backlight.activity_timeout);
        debug_printf("  item.backlight.normal_brightness: %s\r\n", item.backlight.normal_brightness);
        debug_printf("  item.backlight.dim_brightness: %s\r\n", item.backlight.dim_brightness);
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
        return false;
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
            Serial.println("Failed to open config file for writing");
            return false;
        }

        StaticJsonDocument<1024> doc;

        doc["general"]["hostname"] = item.general.hostname;

        doc["ntp"]["hostname"] = item.ntp.hostname;
        doc["ntp"]["gmt_offset"] = item.ntp.gmt_offset;
        doc["ntp"]["daylightsaving"] = item.ntp.daylightsaving;

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
