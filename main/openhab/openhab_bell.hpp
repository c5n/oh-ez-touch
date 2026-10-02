/**
 * @file openhab_bell.hpp
 *
 * The doorbell: one item's state changes, followed wherever the panel is.
 *
 * openhab_events.cpp follows the page on screen, and the bell is on no page --
 * it rings while somebody is looking at the lights. So this is a second,
 * much smaller stream, GET /rest/events?topics=*\/items/<ring>/statechanged,
 * opened only while a ring item is configured: a task, a socket and a 2 KB
 * line buffer, about 8 KB that a panel without a doorbell never spends.
 *
 * A ring is the item changing to ON (a Switch) or OPEN (a Contact). A
 * binding's trigger channel is not an item; a rule or a `timeout` profile
 * turns one into a Switch that goes ON and back -- see doc/configuration.md.
 */
#ifndef OPENHAB_BELL_HPP
#define OPENHAB_BELL_HPP

#include <stdbool.h>

/**
 * Follow `item` on `website` ("http://host:port"), or nothing for an empty
 * one. Cheap to call on every loop: an unchanged pair is ignored. The task is
 * created on the first non-empty call. UI task only.
 */
void openhab_bell_follow(const char *website, const char *item);

/** True once after every ring. Never blocks. */
bool openhab_bell_take_ring(void);

#endif /* OPENHAB_BELL_HPP */
