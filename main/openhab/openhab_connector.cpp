#include "openhab_connector.hpp"

#include "debug.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* JsonVariant::as<const char *>() yields NULL for a missing or non-string
 * value; the comparisons below want an empty string in that case. */
static inline const char *json_str(JsonVariant value)
{
    const char *str = value.as<const char *>();
    return (str != NULL) ? str : "";
}

/* The pool a page's JsonDocument allocates from, laid over whatever scratch
 * the caller passes -- on the panel, the unused tail of the page's own body
 * buffer (see openhab_client.cpp: the body sits at the front of rx_buf and
 * the parse runs while the worker is kept out of it, so the kilobytes behind
 * the body are free to be the document's pool for exactly as long as the
 * parse takes). The pool is one of the two big transient allocations a page
 * load used to make on the heap; the body buffer was the other, and both are
 * the kind of multi-kilobyte ask a days-old fragmented heap stops honouring,
 * which is how page turns died on the wall panels. Neither asks any more.
 *
 * The arena is a bump allocator with a size word per block, which is the
 * whole of what ArduinoJson asks of an Allocator: pool blocks are appended
 * as the document grows, trimmed at the end, and freed newest-first when the
 * document dies. Anything deallocate() cannot honour (an older block) simply
 * stays until the next begin() -- Sitemap::parse() is the only user and it
 * is only ever mid-parse on one task, so nothing outlives a begin().
 *
 * The bounds are the fleet's real pages, measured through the same filter
 * with a counting allocator: every page this installation serves peaks at
 * 4.9 to 5.8 kilobytes of pool. A body of at most six kilobytes therefore
 * leaves room for its document in the twelve the buffer holds; a bigger one
 * fails the parse with a log line and a retry -- deserializeJson() reports
 * NoMemory -- which is the failure mode the heap version already had, except
 * that this one says why. */
class JsonArenaAlloc : public ArduinoJson::Allocator
{
  public:
    void begin(void *mem, size_t size)
    {
        buf  = (uint8_t *)mem;
        cap  = size;
        used = 0;
        last = (size_t)-1;
        free_count = 0;
    }

    void *allocate(size_t size) override
    {
        size = (size + 3) & ~(size_t)3;

        if (size == 0)
            return NULL;

        /* Reuse first: ArduinoJson's strings come and go as alloc/free pairs,
         * and a pure bump allocator would charge the whole of every string
         * ever held to the arena even though almost all of them are returned
         * long before the parse ends. First-fit with a split, which keeps the
         * remainder reusable rather than stranding it. */
        for (size_t i = 0; i < free_count; i++)
        {
            size_t avail = free_list[i].size;

            if (avail < size + 4)
                continue;

            uint32_t *hdr = (uint32_t *)(buf + free_list[i].offset);

            if (avail >= size + 4 + 4 + 16)
            {
                /* Big enough to be worth a remainder block. */
                free_list[i].offset += 4 + size;
                free_list[i].size   -= 4 + size;
            }
            else
            {
                free_list[i] = free_list[--free_count];
            }

            *hdr = (uint32_t)size;
            return hdr + 1;
        }

        if (size + 4 > cap - used)
        {
            /* Said loudly rather than returned quietly: an arena that is too
             * small is a page body that grew past what its tail can document,
             * and that is worth knowing about rather than retrying blind. */
            printf("JsonArena: out of room for a %u byte block (%u of %u used)\r\n",
                   (unsigned)size, (unsigned)used, (unsigned)cap);
            return NULL;
        }

        uint32_t *hdr = (uint32_t *)(buf + used);

        *hdr = (uint32_t)size;
        last = used;
        used += 4 + size;

        return hdr + 1;
    }

    void deallocate(void *ptr) override
    {
        size_t offset = (size_t)((char *)ptr - 4 - (char *)buf);

        /* The newest block just lowers the high-water mark. */
        if (last != (size_t)-1 && offset == last)
        {
            used = last;
            return;
        }

        /* Anything else goes on the free list, or is lost until begin() when
         * that is full -- the document holds a handful of blocks at a time,
         * so full means the parse is an unusual shape, not an unbounded one. */
        if (free_count < FREE_MAX)
        {
            free_list[free_count].offset = offset;
            free_list[free_count].size   = *(uint32_t *)(buf + offset) + 4;
            free_count++;
        }
    }

    void *reallocate(void *ptr, size_t new_size) override
    {
        if (ptr == NULL)
            return allocate(new_size);

        uint32_t *hdr = (uint32_t *)ptr - 1;
        size_t    old_size = *hdr;

        new_size = (new_size + 3) & ~(size_t)3;

        /* In place when it is the newest block -- which is also the common
         * one: the pool trims its tail and grows its block list. */
        if ((char *)hdr == (char *)buf + last && new_size + 4 <= cap - last)
        {
            *hdr = (uint32_t)new_size;
            used = last + 4 + new_size;
            return ptr;
        }

        void *next = allocate(new_size);

        if (next == NULL)
            return NULL;

        memcpy(next, ptr, (old_size < new_size) ? old_size : new_size);
        deallocate(ptr);

        return next;
    }

  private:
    /* Live blocks in flight at once count in the dozens, not the hundreds:
     * the pool's blocks, its block list, and the string nodes between them. */
    static constexpr size_t FREE_MAX = 48;

    struct free_s
    {
        size_t offset;
        size_t size;
    };

    uint8_t *buf;
    size_t   cap = 0;
    size_t   used = 0;
    size_t   last = (size_t)-1;
    free_s   free_list[FREE_MAX];
    size_t   free_count = 0;
};

static JsonArenaAlloc json_arena;

/* A widget's label without the "[state]" openHAB appends to it, and without
 * the run of spaces in front of that.
 *
 * "Kitchen [21.5 degC]" is one string in the sitemap; the panel draws the
 * name and the reading in two different places, so the bracket and everything
 * after it goes.
 *
 * Two things this had wrong. It stepped back one character before looking at
 * anything, so a label that was empty or that began with '[' formed
 * `buffer - 1` -- undefined behaviour, and only harmless by luck. And it
 * passed a plain char to isspace(), which is undefined for any byte with the
 * top bit set: every label with an umlaut in it reached that call with a
 * negative value. glibc happens to tolerate that -- its table is offset for
 * it -- so the host tests cannot demonstrate the second one; newlib, which is
 * what the panel runs, indexes its table directly.
 *
 * Returns `out`, always terminated, so the caller can pass it straight on.
 */
static const char *label_trim(const char *label, char *out, size_t out_size)
{
    const char *end = strchr(label, '[');
    size_t      len = (end != NULL) ? (size_t)(end - label) : strlen(label);

    /* Trailing space, tested as unsigned: isspace() takes an int that must be
     * representable as unsigned char, and a UTF-8 continuation byte in a
     * plain char is not. */
    while (len > 0 && isspace((unsigned char)label[len - 1]))
        len--;

    if (len >= out_size)
        len = out_size - 1;

    memcpy(out, label, len);
    out[len] = '\0';

    return out;
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

/* Turn a sitemap page into the title and the item array.
 *
 * `payload` need not be terminated and is not written to. It has to stay alive
 * for the duration of the call and no longer: every field extracted here goes
 * through one of the strlcpy() setters in the header, so no Item ends up
 * pointing into the page.
 *
 * It is not held for the reason this comment used to give. ArduinoJson 7
 * removed zero-copy parsing -- it copies every string into the document
 * whether the input is `char *` or `const char *`, which is measurable: the
 * same page parsed both ways costs the same 5892 bytes. What the document
 * holds is its own copies.
 *
 * Errors are reported here without the URL, which this does not know; the
 * caller adds it, the way openhab_http.cpp and its callers already divide it
 * up. */
int Sitemap::parse(const char *payload, size_t payload_len, char *scratch,
                   size_t scratch_size)
{
    int retval = 0;
    bool payload_ok = true;

#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
    printf("Sitemap::parse: %u byte payload:\r\n%.*s\r\n",
           (unsigned)payload_len, (int)payload_len, payload);
#endif

    /* What is read out of a page, and therefore all that is stored of it.
     *
     * ArduinoJson 7 documents size themselves -- the old fixed 12000 byte
     * capacity is gone -- and a document that sizes itself to the page grows
     * with whatever openHAB decides to send. A page carries a good deal this
     * panel has no use for: widgetId, visibility, labelSource, staticIcon and
     * unit on every widget; name, label, category, tags, groupNames, members,
     * function and two timestamps on every item; a nested empty "widgets"
     * array; and four more fields inside every linkedPage than the one link
     * that is wanted. All of it was parsed and stored so that the loop below
     * could walk past it.
     *
     * Measured on the six-widget demo page, 4879 bytes of real openHAB 5 JSON,
     * in a 32-bit build with a counting allocator: 5892 bytes and 188
     * allocations without this, 3136 bytes and 103 allocations with it, filter
     * document included. The living-room sub page, 4365 bytes, goes from 5956
     * to 3256. The saving is a page load's whole high-water mark lowered, on
     * the task that also drives the screen.
     *
     * It is the same device SitemapList::parse() below uses on /rest/sitemaps,
     * and it earns its keep twice: what is not named here cannot grow the
     * document however large it gets on the wire, so a future openHAB adding
     * fields costs this panel nothing. Every key named below is one the loop
     * further down actually reads -- add a key there, add it here, or it will
     * read a null.
     *
     * A filter that is an array applies its first element to every element of
     * the input, which is what the widgets and the mappings rely on.
     *
     * Built once: the shape is fixed, and a kilobyte of heap per page load to
     * say so again is exactly the kind of transient this function is trying to
     * stop making. */
    static JsonDocument filter;
    static bool         filter_ready = false;

    if (filter_ready == false)
    {
        filter_ready = true;

        filter["title"] = true;
        /* Whole, not by its "message": the test below is is<JsonObject>(), and an
         * error object filtered down to a key the server did not send would still
         * have to be an object. It is a handful of bytes and only present when the
         * request failed anyway. */
        filter["error"] = true;
        filter["parent"]["link"] = true;

        JsonObject widget_filter = filter["widgets"].add<JsonObject>();

        widget_filter["type"] = true;
        widget_filter["label"] = true;
        widget_filter["icon"] = true;
        widget_filter["minValue"] = true;
        widget_filter["maxValue"] = true;
        widget_filter["step"] = true;
        widget_filter["linkedPage"]["link"] = true;

        JsonObject mapping_filter = widget_filter["mappings"].add<JsonObject>();

        mapping_filter["command"] = true;
        mapping_filter["label"] = true;

        JsonObject item_filter = widget_filter["item"].to<JsonObject>();

        item_filter["link"] = true;
        item_filter["state"] = true;
        item_filter["type"] = true;
        item_filter["groupType"] = true;
        item_filter["transformedState"] = true;

        JsonObject state_filter = item_filter["stateDescription"].to<JsonObject>();

        state_filter["minimum"] = true;
        state_filter["maximum"] = true;
        state_filter["step"] = true;
        state_filter["pattern"] = true;

        /* The other place a Selection's options come from, when the sitemap itself
         * carries no mappings. */
        JsonObject option_filter =
            item_filter["commandDescription"]["commandOptions"].add<JsonObject>();

        option_filter["command"] = true;
        option_filter["label"] = true;
    }

    /* The document's pool: from the caller's scratch when there is some --
     * on the panel the tail of the page's own body buffer, which makes the
     * whole parse allocation-free -- and off the heap when there is none,
     * which is what the host tests and the offline fixtures do. What the
     * arena is and why is at its definition above. */
    ArduinoJson::Allocator *pool = ArduinoJson::detail::DefaultAllocator::instance();

    if (scratch != NULL)
    {
        json_arena.begin(scratch, scratch_size);
        pool = &json_arena;
    }

    JsonDocument doc(pool);

    // Parse JSON object
    /* The length is passed explicitly: the network payload is not
     * terminated. */
    DeserializationError error = deserializeJson(doc, payload, payload_len,
                                                 DeserializationOption::Filter(filter),
                                                 DeserializationOption::NestingLimit(15));

    if (error == DeserializationError::NoMemory && pool == &json_arena)
    {
        /* The page outgrew the tail of its own body buffer. That is a shape
         * the arena comment owns up to, and the right answer to it is the
         * heap after all, not a failed page: a body this big is the rare one,
         * and the transient it costs is the one the arena exists to avoid
         * paying on every page, not on one. */
        printf("Sitemap::parse: page outgrew its arena; parsing from the heap\r\n");

        ArduinoJson::Allocator *heap = ArduinoJson::detail::DefaultAllocator::instance();

        doc = JsonDocument(heap);
        error = deserializeJson(doc, payload, payload_len,
                                DeserializationOption::Filter(filter),
                                DeserializationOption::NestingLimit(15));
    }

    /* Both of these used to "return false", which is 0 and therefore the
     * success code of this function, so the caller kept the stale page and the
     * HTTP client was never closed. */
    if (error)
    {
        printf("Sitemap::parse: deserializeJson() failed: %s\r\n", error.c_str());
        payload_ok = false;
    }
    /* containsKey() is deprecated in ArduinoJson 7. The value is indexed as an
     * object right below, so test for exactly that. */
    else if (doc["error"].is<JsonObject>())
    {
        printf("Sitemap::parse: json error message: %s\r\n", json_str(doc["error"]["message"]));
        payload_ok = false;
    }

    if (payload_ok == true)
    {
#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
        /* ArduinoJson 7 dropped memoryUsage() -- it always returns zero. The
         * serialized size is the closest figure that still says something
         * about how big the page was. */
        printf("Doc serialized size: %u\r\n", (unsigned)measureJson(doc));
#endif

        if (doc["title"])
            strlcpy(title, json_str(doc["title"]), sizeof(title));
        else
            strlcpy(title, "no title", sizeof(title));

#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
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

        /* Scratch for label_trim(), reused across widgets: it is a whole
         * label wide and there is only ever one in flight. */
        char label_buffer[STR_LABEL_LEN];

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
                item->setLabel(label_trim(json_str(widget["label"]), label_buffer,
                                          sizeof(label_buffer)));
            else
                item->setLabel("NO LABEL");

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

/* ------------------------------------------------------------ SitemapList */

void SitemapList::clear()
{
    /* count first, and this is not tidiness: the web server reads this list on
     * its own task while the UI task fills it, unlocked, the same way Config's
     * readers are unlocked and for the same reason. Emptying it before the
     * names change and publishing the new count only once they are all written
     * is what keeps a reader to either the old list or the new one -- never a
     * count that promises more names than have been stored. */
    count = 0;
    total = 0;
}

const char *SitemapList::getName(size_t index) const
{
    return (index < count) ? name[index] : "";
}

const char *SitemapList::getLabel(size_t index) const
{
    return (index < count) ? label[index] : "";
}

int SitemapList::parse(const char *payload, size_t payload_len)
{
    /* The filter. Everything openHAB sends per sitemap -- the link, and a
     * "homepage" object with its own link, its flags and a widget array -- is
     * dropped at the parser, so the document is two strings per sitemap
     * whatever the server puts on the wire. A filter that is an array applies
     * its first element to every element of the input. */
    JsonDocument filter;
    JsonObject   element = filter.add<JsonObject>();

    element["name"] = true;
    element["label"] = true;

    JsonDocument doc;
    /* The length is passed explicitly: the network payload is not
     * terminated. */
    DeserializationError error = deserializeJson(doc, payload, payload_len,
                                                 DeserializationOption::Filter(filter),
                                                 DeserializationOption::NestingLimit(10));

    /* Emptied before anything can fail, so that a refresh that did not work
     * leaves no list behind: the names on offer would be the previous server's,
     * and nothing downstream could tell. */
    clear();

    if (error)
    {
        printf("SitemapList::parse: deserializeJson() failed: %s\r\n", error.c_str());
        return -1;
    }

    JsonArray sitemaps = doc.as<JsonArray>();

    /* Not an array: an openHAB error object, an HTML error page that parsed by
     * accident, or a body from something else entirely at that address. */
    if (sitemaps.isNull())
    {
        printf("SitemapList::parse: not a list of sitemaps\r\n");
        return -1;
    }

    size_t stored = 0;
    size_t seen = 0;

    for (JsonVariant entry : sitemaps)
    {
        const char *entry_name = json_str(entry["name"]);

        /* A sitemap with no name cannot be requested and cannot be stored, so
         * it is not one of the choices and is not counted as one either. */
        if (entry_name[0] == '\0')
            continue;

        seen++;

        /* Counted above and dropped here: the name does not fit the field
         * config.json keeps it in, so offering it would store a truncated one
         * and fetch a page that does not exist. The count and the total then
         * disagree, which is what both front ends report. */
        if (strlen(entry_name) >= STR_SITEMAP_NAME_LEN)
            continue;

        if (stored >= SITEMAP_LIST_COUNT_MAX)
            continue;

        const char *entry_label = json_str(entry["label"]);

        strlcpy(name[stored], entry_name, sizeof(name[stored]));
        strlcpy(label[stored], (entry_label[0] != '\0') ? entry_label : entry_name,
                sizeof(label[stored]));
        stored++;
    }

    /* Both last, for the reason clear() gives, and the count after the total:
     * a reader that catches the pair between the two sees a list one entry
     * short of its own total, which reads as a full list and not as a wrong
     * one. */
    total = seen;
    count = stored;

    return 0;
}
