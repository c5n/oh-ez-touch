/**
 * @file page_model.cpp
 *
 * The connector-independent half of the page model: what an Item does with a
 * state it was given, and what a Sitemap remembers about the widgets of its
 * page. libc only. The half that reads a server's answers is the active
 * connector's -- see page_model.hpp.
 */
#include "page_model.hpp"

#include "debug.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *Item::mappedLabel() const
{
    char *value_end;
    float value = strtof(state_text, &value_end);
    bool  value_is_number = (value_end != state_text && *value_end == '\0');

    for (size_t i = 0; i < mapping_count; i++)
    {
        if (strcmp(selection_command[i], state_text) == 0)
            return selection_label[i];

        /* A number's state has been re-printed through "%f" by the time it
         * gets here ("1.000000"), and the option says "1": compared as
         * numbers when both are numbers. */
        if (value_is_number == true)
        {
            char *option_end;
            float option = strtof(selection_command[i], &option_end);

            if (option_end != selection_command[i] && *option_end == '\0' && option == value)
                return selection_label[i];
        }
    }

    return NULL;
}

void openhab_format_number(char *out, size_t out_size, float value, float step)
{
    int decimals = 0;

    /* As many as the step needs to be exact, which is what openHAB's own
     * UIs send: a step of 0.5 is one decimal, 0.25 two. A step that is not
     * a short decimal at all -- 1/3 -- stops at three. */
    for (float scaled = fabsf(step); decimals < 3; decimals++, scaled *= 10.0f)
        if (fabsf(scaled - roundf(scaled)) < 0.001f)
            break;

    /* Rounded first, so that a value that drifted through a run of float
     * additions prints as the step it is on and not as "-0". */
    float scale = powf(10.0f, (float)decimals);

    value = roundf(value * scale) / scale;

    if (value == 0.0f)
        value = 0.0f;

    snprintf(out, out_size, "%.*f", decimals, (double)value);
}

/* Read one non-negative integer, and say where it stopped.
 *
 * strtol() alone is not enough here: it reports "nothing parsed" only through
 * endptr, and the caller has to know that before it can step over a separator
 * that may not be there. */
static bool parse_uint_field(const char **cursor, long *out)
{
    char *end;
    long  value = strtol(*cursor, &end, 10);

    if (end == *cursor)
        return false;

    *cursor = end;
    *out = value;

    return true;
}

bool Item::getStateHsv(uint16_t *h, uint8_t *s, uint8_t *v) const
{
    const char *cursor = state_text;
    long        parsed[3] = {0, 0, 0};

    /* Black, so a caller that ignores the return value still paints something
     * defined rather than whatever was on its stack. */
    *h = 0;
    *s = 0;
    *v = 0;

    for (size_t i = 0; i < 3; i++)
    {
        if (parse_uint_field(&cursor, &parsed[i]) == false)
            return false;

        /* Step over the separator, but only if there is one. Skipping it
         * blind is what read past the end of a short state. */
        if (i < 2)
        {
            if (*cursor != ',')
                return false;

            cursor++;
        }
    }

    /* Clamped rather than rejected: a hue of 400 is a server being loose with
     * a value that still means something, and lv_color_hsv_to_rgb() takes
     * these as a uint16 and two uint8 that it does not range check. */
    if (parsed[0] < 0)
        parsed[0] = 0;
    if (parsed[0] > 359)
        parsed[0] = 359;

    for (size_t i = 1; i < 3; i++)
    {
        if (parsed[i] < 0)
            parsed[i] = 0;
        if (parsed[i] > 100)
            parsed[i] = 100;
    }

    *h = (uint16_t)parsed[0];
    *s = (uint8_t)parsed[1];
    *v = (uint8_t)parsed[2];

    return true;
}

int Item::applyState(const char *text, size_t len)
{
    char remote_state[STR_STATE_TEXT_LEN];

    /* Copied out rather than used in place: the caller's buffer is a network
     * payload and carries no terminator, and the numeric branch below has to
     * rewrite the value anyway. Truncating to the field width is what
     * HTTPClient::getString() plus strlcpy() did. */
    if (len >= sizeof(remote_state))
        len = sizeof(remote_state) - 1;

    memcpy(remote_state, text, len);
    remote_state[len] = '\0';

    if (   type == ItemType::type_number
        || type == ItemType::type_setpoint
        || type == ItemType::type_slider)
    {
        /* Strip the unit openHAB appends ("21.5 degC"), and re-print with the
         * same format setStateNumber() uses so that the comparison below sees
         * identical text for an unchanged value. Going via a local keeps the
         * source and destination of snprintf() apart. */
        float remote_value = strtof(remote_state, NULL);
        snprintf(remote_state, sizeof(remote_state), "%f", remote_value);
    }

    if (strcmp(state_text, remote_state) == 0)
        return 0;

    strlcpy(state_text, remote_state, sizeof(state_text));

#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
    printf("  update statetext to \"%s\"\r\n", state_text);
#endif

    return 1;
}

void Sitemap::rememberWidget(uint32_t hash)
{
    if (hash == 0)
        return;

    if (widget_id_count >= PAGE_WIDGET_IDS_MAX)
    {
        widget_ids_full = true;
        return;
    }

    widget_ids[widget_id_count++] = hash;
}

bool Sitemap::knowsWidget(uint32_t hash) const
{
    if (widget_ids_full == true)
        return true;

    for (size_t i = 0; i < widget_id_count; i++)
        if (widget_ids[i] == hash)
            return true;

    return false;
}
