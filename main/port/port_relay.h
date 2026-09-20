/**
 * @file port_relay.h
 *
 * The board's mains relays, as an on/off per channel.
 *
 * A port rather than a driver call for the usual reason: how many there are
 * and which pins they sit on is a property of the board, and a desktop has
 * none at all. What sits above this -- the topics they answer to, and what
 * counts as "on" in a payload -- is in peripherals/relay.cpp and is shared.
 *
 * Channels are numbered from 0 here and from 1 in the topic names, which is
 * the one place the two disagree: an array index that starts at 1 is a bug
 * waiting to happen, and a relay called "relay 0" on a wall plate is not what
 * anyone has printed on theirs.
 *
 * Output only. Nothing reads a relay back, because nothing can: the coil is
 * driven from a GPIO and there is no sense line, so the state this firmware
 * believes in is the state it last wrote.
 */
#ifndef PORT_RELAY_H
#define PORT_RELAY_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * How many relays this board has. 0 on a board with none, which is every
 * board but the Lanbon L8-HS and every desktop.
 *
 * Safe to call before port_relay_init(); it answers from the pin table.
 */
unsigned port_relay_count(void);

/**
 * Bring the pins up, all off.
 *
 * Idempotent, and a no-op where the count is 0. Off rather than "as they
 * were": a relay that clicks on by itself during a reboot is the one
 * behaviour a mains switch must not have, and the broker's retained commands
 * put the wanted state back a second later if that is what was asked for.
 */
void port_relay_init(void);

/**
 * Switch one relay.
 *
 * The polarity is the board's business, as it is for the backlight: `true` is
 * a closed contact whichever way the pin has to be driven to get one. Out of
 * range is ignored.
 */
void port_relay_set(unsigned index, bool on);

#ifdef __cplusplus
}
#endif

#endif /* PORT_RELAY_H */
