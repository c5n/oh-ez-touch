#include "openhab_connector.hpp"

#include "debug.h"

#include <ctype.h>
#include <memory>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "openhab_http.hpp"
#include "sim/icon_fixture.hpp"
#include "sim/sim_offline.hpp"
#include "sim/sitemap_fixture.hpp"

/* The largest sitemap page that will be read. openHAB serves a page per
 * navigation level and this firmware renders at most ITEM_COUNT_MAX (6)
 * widgets from one, so the pages are small; the figure is the same order as
 * the 12000 byte document capacity ArduinoJson 6 was given here before it
 * learned to size itself. openhab_http_get() names it in the log when a page
 * does not fit, which is the only way this is ever reached. */
#define SITEMAP_PAGE_BUFFER_SIZE 12288

/* JsonVariant::as<const char *>() yields NULL for a missing or non-string
 * value; the comparisons below want an empty string in that case. */
static inline const char *json_str(JsonVariant value)
{
    const char *str = value.as<const char *>();
    return (str != NULL) ? str : "";
}

/* openHAB offers the same label/command list either as the widget's "mappings"
 * or as the item's "commandOptions"; both are read into the item's fixed
 * selection arrays, so the count has to be clamped to what those hold. */
static void parse_selection(Item *item, JsonVariant array_value)
{
    JsonArray map_array = array_value.as<JsonArray>();
    size_t count = map_array.size();

    if (count > ITEM_SELECTION_COUNT_MAX)
        count = ITEM_SELECTION_COUNT_MAX;

    for (size_t i = 0; i < count; ++i)
    {
        JsonVariant map_elem = map_array[i];
        item->setSelectionLabel(i, json_str(map_elem["label"]));
        item->setSelectionCommand(i, json_str(map_elem["command"]));
    }

    item->setSelectionCount(count);
}

bool Item::stateUrl(char *out, size_t out_size) const
{
    /* Without a link there is no item endpoint to append "/state" to, and the
     * URL that would come out of it -- "/state" -- resolves to something else
     * entirely. Link and group widgets often carry only a page link. */
    if (link[0] == '\0')
        return false;

    int len = snprintf(out, out_size, "%s/state", link);

    return (len > 0 && (size_t)len < out_size);
}

bool Item::iconUrl(const char *website, char *out, size_t out_size) const
{
    /* No icon name is not a failure to report anywhere -- plenty of widgets
     * have none. It just means there is nothing to fetch, and asking openHAB
     * for "/icon/?state=..." would be a 404 per poll. */
    if (icon_name[0] == '\0')
        return false;

    int len = snprintf(out, out_size, "%s/icon/%s?state=%s&format=png",
                       website, icon_name, state_text);

    return (len > 0 && (size_t)len < out_size);
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

int Item::update()
{
    char url[STR_URL_LEN];

    if (stateUrl(url, sizeof(url)) == false)
    {
        printf("Item::update: no state URL for link: %s\r\n", link);
        return -1;
    }

#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
    printf("Item::update: Requesting URL: %s\r\n", url);
#endif
    /* The fixture pages carry a fixed state per item, so there is nothing to
     * poll: leaving the item as it is keeps whatever the UI set locally, which
     * is what makes a switch in offline mode look like it worked. */
    if (sim_offline())
        return 0;

    char remote_state[STR_STATE_TEXT_LEN];
    ssize_t body_len = openhab_http_get(url, remote_state, sizeof(remote_state) - 1, true);

    if (body_len < 0)
    {
        printf("Item::update: ERROR URL: %s\r\n", url);
        return -1;
    }

    return applyState(remote_state, (size_t)body_len);
}

int Item::publish(const char* url)
{
    int retval = 0;

#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
    printf("Item::publish: Requesting URL: %s\r\n", url);
#endif

    /* Nowhere to send it, and nothing that would come back changed. */
    if (sim_offline())
        return 0;

#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
    printf("Item::publish: POST Message: %s\r\n", state_text);
#endif

    if (openhab_http_post_text(url, state_text) != 0)
    {
        printf("Item::publish ERROR URL: %s\r\n", url);
        retval = -1;
    }

    return retval;
}

size_t Item::getIcon(const char* website, unsigned char *buffer, size_t buffer_size)
{
    size_t icon_size = 0;
    char url[STR_URL_LEN];

    /* The name and the state used to be passed in, which meant every caller
     * repeated getIconName() and getStateText() and the URL was built into a
     * buffer one field too narrow to hold it. */
    if (iconUrl(website, url, sizeof(url)) == false)
        return 0;

#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
    printf("Item::getIcon: Requesting URL: %s\r\n", url);
#endif

    if (sim_offline())
    {
        size_t fixture_size = 0;
        const unsigned char *fixture_icon = sim_icon_fixture_get(icon_name, state_text, &fixture_size);

        if (fixture_icon != NULL && fixture_size <= buffer_size)
        {
            memcpy(buffer, fixture_icon, fixture_size);
            icon_size = fixture_size;
        }

        return icon_size;
    }

    /* openHAB serves icons with chunked transfer encoding and no
     * Content-Length. This used to be read off the raw socket through
     * HTTPClient::getStreamPtr(), which meant hand-rolling the framing: a
     * stream->find("\r\n") to skip the first chunk header, and then
     * "icon_size -= 7" at the end to cut off the trailing CRLF "0" CRLF CRLF --
     * a subtraction that was wrong for any icon arriving in more than one
     * chunk, and was marked "ToDo: find a better solution". esp_http_client
     * decodes the framing itself, so all of it is gone and what lands in the
     * buffer is the PNG.
     *
     * A PNG too large for the buffer is no icon rather than a truncated one,
     * which is what the old "insufficient space available. Abort." did. */
    ssize_t read = openhab_http_get(url, buffer, buffer_size, false);

    if (read < 0)
    {
        printf("Item::getIcon: ERROR URL: %s\r\n", url);
        return 0;
    }

    icon_size = (size_t)read;

#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
    printf("Item::getIcon: %u bytes read\r\n", (unsigned)icon_size);
#endif

    return icon_size;
}

int Sitemap::openlink(const char* url)
{
    int retval = 0;
    /* ArduinoJson 7 documents size themselves, so the former fixed 12000 byte
     * capacity is gone; the parser now grows the pool to fit the page. */
    JsonDocument doc;

#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
    printf("Sitemap::openlink: Requesting URL: %s\r\n", url);
#endif

    /* The page outlives the parse: ArduinoJson parses in place and keeps
     * pointers into it, so this buffer has to stay alive until the last
     * json_str() below. On the heap rather than the stack because the UI task
     * has 8 KB of it on the device. */
    const char *payload = NULL;
    size_t payload_len = 0;
    std::unique_ptr<char[]> page;

    if (sim_offline())
    {
        payload = sim_sitemap_fixture_get(url);

        if (payload == NULL)
            printf("Sitemap::openlink: no fixture page for URL: %s\r\n", url);
        else
            payload_len = strlen(payload);
    }
    else
    {
        page.reset(new char[SITEMAP_PAGE_BUFFER_SIZE]);

        ssize_t read = openhab_http_get(url, page.get(), SITEMAP_PAGE_BUFFER_SIZE, false);

        if (read < 0)
        {
            printf("Sitemap::openlink: ERROR URL: %s\r\n", url);
        }
        else
        {
            payload = page.get();
            payload_len = (size_t)read;
        }
    }

    bool payload_ok = (payload != NULL);

#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
    if (payload_ok == true)
        printf("Sitemap::openlink: %u byte payload:\r\n%.*s\r\n",
               (unsigned)payload_len, (int)payload_len, payload);
#endif

    if (payload_ok == true)
    {
        // Parse JSON object
        /* The length is passed explicitly: the network payload is not
         * terminated, and the char * overload parses in place without
         * copying. */
        DeserializationError error = deserializeJson(doc, payload, payload_len,
                                                    DeserializationOption::NestingLimit(15));

        /* Both of these used to "return false", which is 0 and therefore the
         * success code of this function, so the caller kept the stale page and
         * the HTTP client was never closed. */
        if (error)
        {
            printf("Sitemap::openlink: deserializeJson() failed: %s\r\n", error.c_str());
            payload_ok = false;
        }
        /* containsKey() is deprecated in ArduinoJson 7. The value is indexed as
         * an object right below, so test for exactly that. */
        else if (doc["error"].is<JsonObject>())
        {
            printf("Sitemap::openlink: json error message: %s\r\n", json_str(doc["error"]["message"]));
            payload_ok = false;
        }
    }

    if (payload_ok == true)
    {
#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
        /* ArduinoJson 7 dropped memoryUsage() -- it always returns zero. The
         * serialized size is the closest figure that still says something
         * about how big the page was. */
        printf("Doc serialized size: %u\r\n", (unsigned)measureJson(doc));
#endif

        // Save current and last page urls
        strlcpy(last_url, current_url, sizeof(last_url));
        strlcpy(current_url, url, sizeof(current_url));

        if (doc["title"])
            strlcpy(title, json_str(doc["title"]), sizeof(title));
        else
            strlcpy(title, "no title", sizeof(title));

#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
        printf("Sitemap::openlink(\"%s\")\r\n", url);
        printf("  title=\"%s\"\r\n", title);
#endif

        // Cleanup Items
        for (size_t i = 0; i < ITEM_COUNT_MAX; ++i)
        {
            item_array[i].cleanItem();
        }

        item_count = 0;

        // Update Items

        // if current location is a child of the sitemap then set first item
        if (doc["parent"]["link"])
        {
            item_array[item_count].setType(ItemType::type_parent_link);
            item_array[item_count].setPageLink(doc["parent"]["link"]);
#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
            printf("  idx: %u type=parent_link   link=\"%s\"\r\n", (unsigned)item_count,
                   item_array[item_count].getPageLink());
#endif
            item_count++;
        }

        JsonArray widget_array = doc["widgets"].as<JsonArray>();

        for (size_t widget_index = 0; widget_index < widget_array.size(); widget_index++)
        {
            JsonVariant widget = widget_array[widget_index];
            Item* item = &item_array[item_count];

            /* Every lookup walks the object, so the two nodes that are read
             * over and over below are resolved once here. */
            JsonVariant json_item = widget["item"];
            const char *item_type = json_str(json_item["type"]);

            // Label
            if (widget["label"])
            {
                char buffer[STR_LABEL_LEN];
                snprintf(buffer, sizeof(buffer), "%s", json_str(widget["label"]));
                char *end = strchr(buffer, '[');
                if (end == NULL)
                    end = buffer + strlen(buffer);
                end -= 1;
                while(end > buffer && isspace(*end)) end--;
                end[1] = '\0';

                item->setLabel(buffer);
            }
            else
            {
                item->setLabel("NO LABEL");
            }

#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
            printf("  idx: %u label=\"%s\"", (unsigned)item_count, item->getLabel());
#endif

            // Icon
            if (widget["icon"])
            {
                item->setIconName(widget["icon"]);
#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
                printf("  icon=\"%s\"", item->getIconName());
#endif
            }

            // Type
            item->setType(ItemType::type_unknown);

            if (widget["type"] == "Text")
            {
                if (widget["linkedPage"]["link"])
                    item->setType(ItemType::type_link);
                // >= <= needed to distinct from strings
                else if (   item_type[0] != '\0'
                         && strcmp(item_type, "Number") >= 0
                         && strcmp(item_type, "Number:Z") <= 0)
                    item->setType(ItemType::type_number);
                else
                    item->setType(ItemType::type_string);
            }
            else if (widget["type"] == "Group")
            {
                item->setType(ItemType::type_group);
            }
            else if (widget["type"] == "Switch")
            {
                if (strcmp(item_type, "Switch") == 0)
                    item->setType(ItemType::type_switch);
                else if (strcmp(item_type, "Rollershutter") == 0)
                    item->setType(ItemType::type_rollershutter);
                else if (strcmp(item_type, "Player") == 0)
                    item->setType(ItemType::type_player);
                else if (strcmp(item_type, "Group") == 0)
                {
                    const char *group_type = json_str(json_item["groupType"]);

                    if (strcmp(group_type, "Switch") == 0)
                        item->setType(ItemType::type_switch);
                    else if (strcmp(group_type, "Rollershutter") == 0)
                        item->setType(ItemType::type_rollershutter);
                }
            }
            else if (widget["type"] == "Setpoint")
                item->setType(ItemType::type_setpoint);
            else if (widget["type"] == "Slider")
                item->setType(ItemType::type_slider);
            else if (widget["type"] == "Selection")
                item->setType(ItemType::type_selection);
            else if (widget["type"] == "Colorpicker")
                item->setType(ItemType::type_colorpicker);

#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
            printf("  type=%u", item->getType());
#endif

            // MinVal
            if (widget["minValue"])
                item->setMinVal(widget["minValue"].as<float>());
            else if (json_item["stateDescription"]["minimum"])
                item->setMinVal(json_item["stateDescription"]["minimum"].as<float>());
            else
                item->setMinVal(0.0f);
#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
            printf("  minVal=%.2f", item->getMinVal());
#endif

            // MaxVal
            if (widget["maxValue"])
                item->setMaxVal(widget["maxValue"].as<float>());
            else if (json_item["stateDescription"]["maximum"])
                item->setMaxVal(json_item["stateDescription"]["maximum"].as<float>());
            else
                item->setMaxVal(100.0f);
#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
            printf("  maxVal=%.2f", item->getMaxVal());
#endif

            // Step
            if (widget["step"])
                item->setStep(widget["step"].as<float>());
            else if (json_item["stateDescription"]["step"])
                item->setStep(json_item["stateDescription"]["step"].as<float>());
            else
                item->setStep(1.0f);
#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
            printf("  step=%.2f", item->getStep());
#endif

            // Number format string
            if (json_item["stateDescription"]["pattern"])
                item->setNumberPattern(json_item["stateDescription"]["pattern"]);
            else
                item->setNumberPattern("%d");
#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
            printf("  numpat=\"%s\"", item->getNumberPattern());
#endif

            // State
            if (json_item["state"])
            {
                if (   item->getType() == ItemType::type_number
                    || item->getType() == ItemType::type_setpoint
                    || item->getType() == ItemType::type_slider)
                {
                    // convert number to get rid of unit
                    item->setStateNumber(strtof(json_str(json_item["state"]), NULL));
#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
                    printf("  num-statetext=\"%s\"", item->getStateText());
#endif
                }
                else
                {
                    item->setStateText(json_item["state"]);
#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
                    printf("  statetext=\"%s\"", item->getStateText());
#endif
                }
            }

            // Transformed State
            if (json_item["transformedState"])
            {
                item->setTransformedStateText(json_item["transformedState"]);
            }

            // Links
            if (widget["linkedPage"]["link"])
            {
                item->setPageLink(widget["linkedPage"]["link"]);
#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
                printf("  page_link=\"%s\"", item->getPageLink());
#endif
            }

            if (json_item["link"])
            {
                item->setLink(json_item["link"]);
#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
                printf("  link=\"%s\"", item->getLink());
#endif
            }

            // Mappings
            if (widget["mappings"])
                parse_selection(item, widget["mappings"]);
            else if (json_item["commandDescription"]["commandOptions"])
                parse_selection(item, json_item["commandDescription"]["commandOptions"]);

#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
            printf("\r\n");
#endif

            item_count++;

            if (item_count >= ITEM_COUNT_MAX)
                break;
        }
    }
    else
    {
        retval = -1;
    }

    return retval;
}
