#ifndef AC_SETTINGS_H
#define AC_SETTINGS_H

#include "AutoConnect.h"
#include "config.hpp"

#ifndef DEBUG_AC_SETTINGS
#define DEBUG_AC_SETTINGS 0
#endif

Config *current_config;

// Declare AutoConnectElements for the page asf /openhab_setting
ACText(config_general_header, "<h2>General</h2>");
ACInput(config_general_hostname, "", "Hostname", "^[^/:]*$");

ACText(config_ntp_header, "<h2>NTP Time</h2>");
ACInput(config_ntp_host, "", "Host", "^[^/:]*$");
ACInput(config_ntp_gmt_offset, "", "GMT Offset", "^-?[0-9]*$", "0");
ACCheckbox(config_ntp_daylightsaving, "daylightsaving", "Daylight Saving (+1h)");

ACText(config_backlight_header, "<h2>LCD Backlight Dimming</h2>");
ACInput(config_backlight_timeout, "", "Activity timeout [s] (0=off)", "^[0-9]*$", "0");
ACInput(config_backlight_normal_brightness, "", "Normal Brightness [%]", "^([0-9]|[1-9][0-9]|100)$", "100");
ACInput(config_backlight_dim_brightness, "", "Dim Brightness [%]", "^([0-9]|[1-9][0-9]|100)$", "60");

ACText(config_beeper_header, "<h2>Beeper</h2>");
ACCheckbox(config_beeper_enabled, "beeper_enabled", "Enable Beeper");

ACText(config_openhab_header, "<h2>Openhab Server</h2>");
ACInput(config_openhab_host, "", "Host", "^[^/:]*$");
ACInput(config_openhab_port, "", "Port", "^[0-9]*$", "8080");
ACInput(config_openhab_sitemap, "", "Sitemap", "^[^/:]*$");

ACText(config_openhab_sensor_header, "<h2>Sensors</h2>");
ACCheckbox(config_openhab_sensor_bme280_use, "bme280", "Use BME280 Sensor");
ACInput(config_openhab_sensor_bme280_interval, "", "Update interval [s]", "^[0-9]*$", "180");
ACInput(config_openhab_sensor_bme280_item_temperature, "", "Temperature Item", "^[^/:]*$");
ACInput(config_openhab_sensor_bme280_item_humidity, "", "Humidity Item", "^[^/:]*$");
ACInput(config_openhab_sensor_bme280_item_pressure, "", "Pressure Item", "^[^/:]*$");

ACSubmit(config_openhab_save, "Save", "openhab_settings_save");

// Declare the custom Web page as /openhab_setting and contains the AutoConnectElements
AutoConnectAux openhab_settings("/openhab_settings", "OpenHAB Settings", true,
                                {config_general_header,
                                 config_general_hostname,
                                 config_ntp_header,
                                 config_ntp_host,
                                 config_ntp_gmt_offset,
                                 config_ntp_daylightsaving,
                                 config_backlight_header,
                                 config_backlight_timeout,
                                 config_backlight_normal_brightness,
                                 config_backlight_dim_brightness,
                                 config_beeper_header,
                                 config_beeper_enabled,
                                 config_openhab_header,
                                 config_openhab_host,
                                 config_openhab_port,
                                 config_openhab_sitemap,
                                 config_openhab_sensor_header,
                                 config_openhab_sensor_bme280_use,
                                 config_openhab_sensor_bme280_interval,
                                 config_openhab_sensor_bme280_item_temperature,
                                 config_openhab_sensor_bme280_item_humidity,
                                 config_openhab_sensor_bme280_item_pressure,
                                 config_openhab_save});

// Declare AutoConnectElements for the page as /openhab_settings_save
ACText(config_openhab_caption2, "<h4>Settings saved.</h4>", "text-align:center;color:#2f4f4f;padding:10px;");
ACText(parameters);
ACSubmit(resetrequest, "Reset device to apply settings", "/_ac/reset");

// Declare the custom Web page as /openhab_settings_save and contains the AutoConnectElements
AutoConnectAux openhab_settings_save("/openhab_settings_save", "OpenHAB Settings save", false,
                                     {config_openhab_caption2,
                                      parameters,
                                      resetrequest});

String ac_settings_handler(AutoConnectAux &aux, PageArgument &args)
{
    aux["config_general_hostname"].value = String(current_config->item.general.hostname);

    aux["config_ntp_host"].value = String(current_config->item.ntp.hostname);
    aux["config_ntp_gmt_offset"].value = String(current_config->item.ntp.gmt_offset);
    aux["config_ntp_daylightsaving"].as<AutoConnectCheckbox>().checked = current_config->item.ntp.daylightsaving;

    aux["config_backlight_timeout"].value = String(current_config->item.backlight.activity_timeout);
    aux["config_backlight_normal_brightness"].value = String(current_config->item.backlight.normal_brightness);
    aux["config_backlight_dim_brightness"].value = String(current_config->item.backlight.dim_brightness);

    aux["config_beeper_enabled"].as<AutoConnectCheckbox>().checked = current_config->item.beeper.enabled;

    aux["config_openhab_host"].value = String(current_config->item.openhab.hostname);
    aux["config_openhab_port"].value = String(current_config->item.openhab.port);
    aux["config_openhab_sitemap"].value = String(current_config->item.openhab.sitemap);

    aux["config_openhab_sensor_bme280_use"].as<AutoConnectCheckbox>().checked = current_config->item.openhab.sensors.bme280.use;
    aux["config_openhab_sensor_bme280_interval"].value = String(current_config->item.openhab.sensors.bme280.interval);
    aux["config_openhab_sensor_bme280_item_temperature"].value = String(current_config->item.openhab.sensors.bme280.items.temperature);
    aux["config_openhab_sensor_bme280_item_humidity"].value = String(current_config->item.openhab.sensors.bme280.items.humidity);
    aux["config_openhab_sensor_bme280_item_pressure"].value = String(current_config->item.openhab.sensors.bme280.items.pressure);

    return String();
}

String ac_settings_save_handler(AutoConnectAux &aux, PageArgument &args)
{
#if DEBUG_AC_SETTINGS
    debug_printf("openhab_settings_save_handler\n");
#endif

    args.arg("config_general_hostname").toCharArray(current_config->item.general.hostname, sizeof(current_config->item.general.hostname));

    args.arg("config_ntp_host").toCharArray(current_config->item.ntp.hostname, sizeof(current_config->item.ntp.hostname));
    current_config->item.ntp.gmt_offset = args.arg("config_ntp_gmt_offset").toInt();
    current_config->item.ntp.daylightsaving = args.hasArg("config_ntp_daylightsaving") ? true : false;

    current_config->item.backlight.activity_timeout = args.arg("config_backlight_timeout").toInt();
    current_config->item.backlight.normal_brightness = args.arg("config_backlight_normal_brightness").toInt();
    current_config->item.backlight.dim_brightness = args.arg("config_backlight_dim_brightness").toInt();

    current_config->item.beeper.enabled = args.hasArg("config_beeper_enabled") ? true : false;

    args.arg("config_openhab_host").toCharArray(current_config->item.openhab.hostname, sizeof(current_config->item.openhab.hostname));
    current_config->item.openhab.port = args.arg("config_openhab_port").toInt();
    args.arg("config_openhab_sitemap").toCharArray(current_config->item.openhab.sitemap, sizeof(current_config->item.openhab.sitemap));

    current_config->item.openhab.sensors.bme280.use = args.hasArg("config_openhab_sensor_bme280_use") ? true : false;
    current_config->item.openhab.sensors.bme280.interval = args.arg("config_openhab_sensor_bme280_interval").toInt();
    args.arg("config_openhab_sensor_bme280_item_temperature").toCharArray(current_config->item.openhab.sensors.bme280.items.temperature, sizeof(current_config->item.openhab.sensors.bme280.items.temperature));
    args.arg("config_openhab_sensor_bme280_item_humidity").toCharArray(current_config->item.openhab.sensors.bme280.items.humidity, sizeof(current_config->item.openhab.sensors.bme280.items.humidity));
    args.arg("config_openhab_sensor_bme280_item_pressure").toCharArray(current_config->item.openhab.sensors.bme280.items.pressure, sizeof(current_config->item.openhab.sensors.bme280.items.pressure));

    // Echo back saved parameters to AutoConnectAux page.
    AutoConnectText &echo = aux["parameters"].as<AutoConnectText>();
    echo.value = "<b>General</b><br>\r\n";
    echo.value += "Hostname: " + String(current_config->item.general.hostname) + "<br>\r\n";
    echo.value += "<b>NTP</b><br>\r\n";
    echo.value += "Server: " + String(current_config->item.ntp.hostname) + "<br>\r\n";
    echo.value += "GMT Offset: " + String(current_config->item.ntp.gmt_offset) + "<br>\r\n";
    echo.value += "Daylightsaving: " + String(current_config->item.ntp.daylightsaving) + "<br>\r\n";
    echo.value += "<b>Backlight</b><br>\r\n";
    echo.value += "Activity Timeout: " + String(current_config->item.backlight.activity_timeout) + "<br>\r\n";
    echo.value += "Normal Brightness: " + String(current_config->item.backlight.normal_brightness) + "<br>\r\n";
    echo.value += "Dim Brightness: " + String(current_config->item.backlight.dim_brightness) + "<br>\r\n";
    echo.value += "<b>Beeper</b><br>\r\n";
    echo.value += "Enable Beeper: " + String(current_config->item.beeper.enabled) + "<br>\r\n";
    echo.value += "<b>Openhab</b><br>\r\n";
    echo.value += "Server: " + String(current_config->item.openhab.hostname) + "<br>\r\n";
    echo.value += "Port: " + String(current_config->item.openhab.port) + "<br>\r\n";
    echo.value += "Sitemap: " + String(current_config->item.openhab.sitemap) + "<br>\r\n";
    echo.value += "<b>Sensors</b><br>\r\n";
    echo.value += "BME280 use: " + String(current_config->item.openhab.sensors.bme280.use) + "<br>\r\n";
    echo.value += "BME280 interval: " + String(current_config->item.openhab.sensors.bme280.interval) + "<br>\r\n";
    echo.value += "BME280 temperature item: " + String(current_config->item.openhab.sensors.bme280.items.temperature) + "<br>\r\n";
    echo.value += "BME280 humidity item: " + String(current_config->item.openhab.sensors.bme280.items.humidity) + "<br>\r\n";
    echo.value += "BME280 pressure item: " + String(current_config->item.openhab.sensors.bme280.items.pressure) + "<br>\r\n";

#if DEBUG_AC_SETTINGS
    debug_printf("%s\n", echo.value.c_str());
#endif

    current_config->saveConfig();

    return String("");
}

void ac_settings_setup(Config *config)
{
    current_config = config;
}

#endif
