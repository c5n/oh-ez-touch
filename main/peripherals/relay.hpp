#ifndef RELAY_HPP
#define RELAY_HPP

/**
 * @file relay.hpp
 *
 * The board's relays, over MQTT and nothing else.
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

#endif /* RELAY_HPP */
