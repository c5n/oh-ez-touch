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
 * publish it, and peripherals/sensor_main.cpp is the one place that knows a new
 * one was just taken. Silently does nothing when there is no connection.
 */
void ohez_mqtt_publish_bme280(float temperature_c, float humidity_pct, float pressure_hpa);

/** Whether there is a broker connection right now.
 *
 * For a caller that keeps state about what it has already published and has to
 * throw that away when a new session begins: a reconnected broker holds none of
 * the last session's retained messages. ble/ble_scan.cpp watches the edge. */
bool ohez_mqtt_connected(void);

/**
 * Publish one value under this device's prefix, e.g. "ble/aabbccddeeff/rssi".
 *
 * The seam for a module that owns a subtree of the topic tree rather than a
 * fixed handful of topics -- the beacon scanner, whose topics are one per
 * advertiser in range and cannot be a list in this file. Retained according to
 * the setting, QoS 0, like everything else here.
 *
 * @return false when there is no connection, in which case nothing was sent.
 *   Callers that track what the broker holds need to know the difference.
 */
bool ohez_mqtt_publish_value(const char *suffix, const char *value);

/**
 * Remove a retained topic: a zero-length payload, published retained, which is
 * how MQTT deletes a broker's stored message.
 *
 * Retained whatever the setting says -- a clear that is not retained deletes
 * nothing -- and harmless on a topic that was never retained.
 */
bool ohez_mqtt_clear_value(const char *suffix);

/**
 * A command that arrived on a subscribed topic.
 *
 * @param topic the part after this device's prefix, e.g. "relay/2/set", so a
 *   handler that registered a wildcard can tell which topic it matched.
 * @param value the payload, NUL-terminated and truncated to fit.
 *
 * Called from ohez_mqtt_loop(), so on the task that owns Config, LVGL and the
 * display -- never on the client's own task. A handler may therefore do
 * whatever the main loop may do.
 */
typedef void (*ohez_mqtt_command_fn)(const char *topic, const char *value);

/**
 * Take ownership of a subtree of the command tree: the counterpart to
 * ohez_mqtt_publish_value() for the direction the broker talks in.
 *
 * @param filter relative to this device's prefix and in MQTT's own filter
 *   syntax, e.g. "relay/+/set". Not copied -- pass a literal or something
 *   that outlives the client.
 * @param handler called once per matching message, in registration order if
 *   two filters overlap.
 *
 * Register during setup, before the first ohez_mqtt_loop(): the client
 * subscribes on connect, and a subscription belongs to a session rather than
 * to the client, so registering late is legal but does not reach the broker
 * until the next reconnect. That case is handled anyway -- a registration
 * made while connected subscribes immediately -- because the alternative is a
 * silence with no cause to find.
 *
 * @return false when the table is full, in which case nothing was registered
 *   and that subtree will never be delivered.
 */
bool ohez_mqtt_subscribe(const char *filter, ohez_mqtt_command_fn handler);

/**
 * The same, for a subtree whose messages are events rather than states.
 *
 * Identical in every way but one: a message the broker delivers because it was
 * *retained* is not passed on. MQTT sets the retain flag on delivery only when
 * the message comes out of the broker's store in answer to a fresh
 * subscription -- a live publish to an established subscription arrives with it
 * clear -- so this is exactly "somebody asked for this just now" against
 * "somebody asked for this at some point in the past".
 *
 * That distinction is a feature for a relay, which wants its retained `set`
 * replayed after a reboot so the panel comes back in the state the installation
 * thinks it is in. It is a defect for anything that makes a noise or shows a
 * banner: a panel that chirps every time the broker restarts is a panel
 * somebody unplugs. ui/ui_beep.cpp is the caller, and the reasoning is written
 * out at greater length there.
 */
bool ohez_mqtt_subscribe_live(const char *filter, ohez_mqtt_command_fn handler);

#endif // OHEZ_MQTT_HPP
