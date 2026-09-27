#ifndef RELAY_HPP
#define RELAY_HPP

/**
 * @file relay.hpp
 *
 * The board's relays: over MQTT, and a readout on the Systeminfo page.
 *
 * There is no setting for these and no widget: a relay answers
 * `<prefix>/relay/<n>/set` and reports on `<prefix>/relay/<n>`, and that is
 * the whole of the interface. The topics and the payloads are in relay.cpp.
 *
 * Both calls are no-ops on a board with no relays, so main.cpp does not guard
 * them.
 */

/** Bring the relays up, all off, and claim their topics. Before the first
 * ohez_mqtt_loop(), so the subscription is in place when the client
 * connects. */
void relay_setup(void);

/** Publish whatever has changed, and everything again after a reconnect.
 * Commands are applied from inside ohez_mqtt_loop(), so call this after it. */
void relay_loop(void);

/** How many relays this board has. Zero on every board but the Lanbon L8-HS,
 * which is why nothing else needs to guard these two. */
unsigned relay_count(void);

/** What relay `index` is set to, 0-based like port_relay.h and unlike the
 * MQTT topics, which are numbered from what is printed on the wall plate.
 * False past the count and on a board with none. The answer is the state the
 * firmware last wrote -- there is no sense line back from the coil, which
 * port_relay.h says outright. */
bool relay_state(unsigned index);

#endif /* RELAY_HPP */
