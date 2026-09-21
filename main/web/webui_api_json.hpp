#ifndef WEBUI_API_JSON_HPP
#define WEBUI_API_JSON_HPP

/**
 * @file webui_api_json.hpp
 *
 * The JSON the REST API speaks, built and parsed with nothing but ArduinoJson,
 * libc and the settings table -- no transport, no LVGL, no network. This is
 * the webui_api.cpp half that the host tests can reach, split out for the
 * same reason multipart.c was: what decides what a request *means* should not
 * need a server to be checked.
 *
 * Every producer supports two passes: called with out == NULL it returns the
 * number of bytes the document needs (excluding the terminator), called with
 * a buffer it writes it. The handlers measure, malloc, then fill, so nothing
 * is ever truncated into invalid JSON.
 *
 * webui_api_config_apply() is the exception -- it mutates, so it cannot be
 * called twice. Its response is bounded by the request body plus a fixed
 * overhead; WEBUI_API_APPLY_RESPONSE_OVERHEAD says how much.
 */

#include "config/config_fields.hpp"
#include "ui/ui_beep.hpp"

#include <stddef.h>

/* What GET /api/status reports. The handler fills it from the port layer;
 * this module only turns it into JSON, which is what keeps this header free
 * of port_net.h. */
struct webui_api_status_s
{
    char               version[8]; /* "0.91" */
    const char        *target;     /* TARGET_NAME */
    const char        *build;      /* __DATE__ " " __TIME__ */
    unsigned long long uptime_s;
    const char        *hostname;
    const char        *ssid;
    const char        *bssid;
    bool               wired;
    int                rssi;       /* dBm; meaningless when wired */
    const char        *ip;
    const char        *mac;
    unsigned long      free_heap;
};

size_t webui_api_status_json(const struct webui_api_status_s *st, char *out, size_t out_size);

/* GET /api/config: every row of config_fields[] with its label, kind, current
 * value and, where they exist, the enum's options and the numeric range --
 * enough for a client to render the whole settings form without knowing the
 * table. SETTINGS_F_SECRET values are masked as "***", the same rule the test
 * interface and the MQTT client already enforce. */
size_t webui_api_config_json(const config_item_t *item, char *out, size_t out_size);

/* POST /api/config: a JSON object of {name: value}. Applies every field
 * through the same setters the web form and the MQTT client use, so a value
 * the form rejects is not one this accepts. Absent fields are untouched --
 * unlike POST /save, where an absent checkbox means "off".
 *
 * All or nothing: one rejected field rolls the whole request back, and
 * nothing is persisted. A SETTINGS_F_SECRET field sent the masked value
 * "***" is a no-op -- it is the echo of a GET, not a new password.
 *
 * @return true when everything was applied (the response in `out` is
 *   {"applied":[...]}), false when anything was rejected (`out` is
 *   {"rejected":{name:reason}} and `item` is unchanged).
 *
 * `out` must hold body_len + WEBUI_API_APPLY_RESPONSE_OVERHEAD bytes; the
 * response repeats names out of the body, so that is the bound. */
#define WEBUI_API_APPLY_RESPONSE_OVERHEAD 256

bool webui_api_config_apply(config_item_t *item, const char *body, size_t body_len,
                            char *out, size_t out_size);

/* GET /api/sounds: the vocabulary, out of ui_sound_names[]. */
size_t webui_api_sounds_json(char *out, size_t out_size);

/* POST /api/sound: {"name":"door_chime","force":true}. `force` plays past the
 * runtime mute -- the locate use case -- and defaults to false.
 *
 * @return true with *sound_out set when the name is in the vocabulary (`out`
 *   is {"queued":name}), false otherwise (`out` is a 400 body naming the
 *   valid sounds, so a typo is said rather than silently ignored -- the same
 *   argument sound_command() makes in ui_beep.cpp). */
bool webui_api_sound_parse(const char *body, size_t body_len,
                           enum ui_sound_e *sound_out, bool *force_out,
                           char *out, size_t out_size);

#endif /* WEBUI_API_JSON_HPP */
