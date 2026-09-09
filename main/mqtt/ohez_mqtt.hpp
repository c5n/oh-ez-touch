#ifndef OHEZ_MQTT_HPP
#define OHEZ_MQTT_HPP

/**
 * @file ohez_mqtt.hpp
 *
 * The MQTT client: what this panel tells a broker about itself, and the one
 * way the broker can talk back.
 *
 * Named ohez_* rather than mqtt_* because esp-mqtt's public header is
 * "mqtt_client.h" and main/ is on this component's own include path -- an
 * ohez_mqtt.hpp cannot be mistaken for it, in either direction.
 *
 * The topic layout, and why it is what it is, is in ohez_mqtt.cpp.
 */

#include "config/config.hpp"

/** Record the settings. Does not connect: that happens on the first
 * ohez_mqtt_loop() after the link is up, which is why main.cpp calls the loop
 * from inside its online guard. */
void ohez_mqtt_setup(Config *config);

/**
 * Connect if the settings say to, publish what is due, and apply whatever the
 * broker asked for since the last call.
 *
 * Everything that touches Config, LVGL or the display happens here, on the
 * caller's task. The client's own task only ever records what arrived.
 */
void ohez_mqtt_loop(Config &config);

/**
 * The settings changed. Only records the request.
 *
 * Called from settings_apply_live(), so from the web server's task as well as
 * the panel's: reconnecting to a broker and republishing three dozen retained
 * topics is not something to do from a POST handler. The next
 * ohez_mqtt_loop() restarts the client if the broker moved, and republishes
 * the settings either way.
 */
void ohez_mqtt_request_reconfigure(void);

/**
 * One fresh BME280 reading.
 *
 * Pushed rather than polled: a reading exists for as long as it takes to
 * publish it, and openhab/openhab_sensor_main.cpp is the one place that knows
 * a new one was just taken. Silently does nothing when there is no connection.
 */
void ohez_mqtt_publish_bme280(float temperature_c, float humidity_pct, float pressure_hpa);

#endif // OHEZ_MQTT_HPP
