/**
 * @file openhab_discover.hpp
 *
 * Which openHAB servers are on this network.
 *
 * openHAB announces itself over mDNS -- `_openhab-server._tcp.local`, carrying
 * the port it serves REST on -- which is how its own phone apps find a server,
 * and it means the panel's first setting need not be typed from an address
 * somebody had to look up. Ask, list what answers, touch one: the host and the
 * port are filled in, and the sitemap list in openhab_sitemaps.cpp fills the
 * row under them from the server just chosen. That is the whole of configuring
 * openHAB on a panel with no keyboard.
 *
 * Shaped like openhab_sitemaps.hpp next door, and for the same reasons: one
 * cache both front ends read, a request that only records a want because it is
 * called from two different tasks, and a revision the settings screen watches
 * so it redraws when the answer moves rather than on a timer.
 *
 * What is different is underneath. This does not go through the openHAB client
 * task -- there is no HTTP in it -- but over a UDP socket of its own, read
 * from the loop without blocking, in the manner of the WLAN scan it stands
 * beside on the screen. A scan is one 44 byte packet, sent twice against the
 * loss that multicast over WiFi is prone to, and a window of a couple of
 * seconds to hear the answers.
 *
 * It finds what announces itself, which is not always everything: a server
 * behind an access point that filters multicast, or in a Docker bridge
 * network, will not be heard. So this fills a field that can still be typed,
 * exactly as the sitemap list does.
 */
#ifndef OPENHAB_DISCOVER_HPP
#define OPENHAB_DISCOVER_HPP

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* More openHAB servers than a home has, and a hard bound on what one scan can
 * return. The same figure, and the same reasoning, as the WLAN scan's. */
#define OPENHAB_DISCOVER_COUNT_MAX 8

enum openhab_discover_state_e
{
    OPENHAB_DISCOVER_IDLE = 0, /* never looked */
    OPENHAB_DISCOVER_SCANNING,
    OPENHAB_DISCOVER_READY,    /* the window closed; the list is what answered */
    OPENHAB_DISCOVER_FAILED    /* the question could not even be asked */
};

/**
 * Look for servers, from any task.
 *
 * Records the want; openhab_discover_loop() opens the socket and asks. A
 * request that arrives while a scan is running is ignored rather than
 * restarting it -- unlike the sitemap list, where the endpoint may have
 * changed under it, the question here is always the same question.
 */
void openhab_discover_request(void);

/**
 * Send, listen and close the window. Called unconditionally from the main
 * loop.
 */
void openhab_discover_loop(void);

enum openhab_discover_state_e openhab_discover_state(void);

size_t openhab_discover_count(void);

/* "" or 0 for an index that is not there, so a caller can print the result
 * without testing it first.
 *
 * The host is the address the answer came from, as digits, and not the name
 * the server gave for itself: that name is a `.local` one, which the panel has
 * no way to resolve when it later asks this address for a sitemap. The label
 * is the instance name openHAB advertises, which is worth showing because it
 * is the one human-readable thing in the answer. */
const char *openhab_discover_host(size_t index);
uint16_t    openhab_discover_port(size_t index);
const char *openhab_discover_label(size_t index);

/* Bumped whenever the state or the list changes, and never zero. */
uint32_t openhab_discover_revision(void);

#endif /* OPENHAB_DISCOVER_HPP */
