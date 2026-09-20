#ifndef WEBUI_API_HPP
#define WEBUI_API_HPP

/**
 * @file webui_api.hpp
 *
 * The REST half of the web interface, as four routes:
 *
 *   GET  /api/status   version, target, uptime, network, heap -- everything a
 *                      fleet manager polls, and deliberately without the side
 *                      effects GET / has: opening the page is what asks the
 *                      panel for its sitemap list and an mDNS scan, and a
 *                      poller doing that every ten seconds would be load the
 *                      page was never meant to cause.
 *   GET  /api/config   every setting with label, kind, value and range or
 *                      options -- enough to render the form without knowing
 *                      the table. Secrets are masked.
 *   POST /api/config   a JSON object of changed settings. Absent fields are
 *                      untouched -- unlike POST /save, where an absent
 *                      checkbox means "off" -- and one rejected value rolls
 *                      the whole request back.
 *   GET  /api/sounds   the sound vocabulary.
 *   POST /api/sound    play one, forced past the mute when asked: the locate
 *                      chime, for the panel whose position is unknown.
 *
 * All of the deciding is in webui_api_json.cpp, which includes nothing a host
 * test cannot link. What is left here is the transport: filling structs from
 * the port layer, locking Config, and the two steps a save takes afterwards
 * -- persist, then re-apply -- in the same order webui_handle_save() takes
 * them.
 */

#include "config/config.hpp"

void webui_api_setup(Config *config);

#endif /* WEBUI_API_HPP */
