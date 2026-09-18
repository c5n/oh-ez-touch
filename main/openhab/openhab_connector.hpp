#ifndef OPENHAB_CONNECTOR_H
#define OPENHAB_CONNECTOR_H

#include <ArduinoJson.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ITEM_COUNT_MAX 6

#define ITEM_SELECTION_COUNT_MAX 10
#define ITEM_SELECTION_COMMAND_LEN_MAX 20
#define ITEM_SELECTION_LABEL_LEN_MAX 20

#define STR_LABEL_LEN 32
#define STR_ICON_NAME_LEN 32
#define STR_STATE_TEXT_LEN 32
#define STR_TRANSFORMEDSTATE_TEXT_LEN 32
#define STR_PATTERN_LEN 16
#define STR_LINK_LEN 128
#define STR_TITLE_LEN 32

/* Every URL this client builds, at one width.
 *
 * The widest of the three is the icon:
 *   <website>/icon/<icon_name>?state=<state_text>&format=png
 * which with the fields above needs 128 + 6 + 32 + 7 + 32 + 12 = 217. It used
 * to be built into a STR_LINK_LEN buffer, so a long host and a long state
 * truncated it silently -- and a truncated URL fails every request, which the
 * connection-error statistics then count. */
#define STR_URL_LEN 256


enum ItemType
{
    type_unknown,
    type_parent_link,
    type_link,
    type_group,
    type_number,
    type_string,
    type_setpoint,
    type_slider,
    type_selection,
    type_colorpicker,
    type_switch,
    type_rollershutter,
    type_player
};

/* The setters of Item and Sitemap copy strings of unknown length straight out
 * of the sitemap JSON, so they use strlcpy(): it truncates like strncpy() but,
 * unlike it, always terminates the destination. */
class Item
{
private:
    char label[STR_LABEL_LEN];
    char icon_name[STR_ICON_NAME_LEN];
    enum ItemType type = type_unknown;
    char state_text[STR_STATE_TEXT_LEN];
    char transformedstate_text[STR_TRANSFORMEDSTATE_TEXT_LEN];
    char pattern[STR_PATTERN_LEN];
    float min_val = 0.0f;
    float max_val = 0.0f;
    float step_val = 1.0f;
    char selection_command[ITEM_SELECTION_COUNT_MAX][ITEM_SELECTION_COMMAND_LEN_MAX] = {};
    char selection_label[ITEM_SELECTION_COUNT_MAX][ITEM_SELECTION_LABEL_LEN_MAX] = {};
    size_t mapping_count = 0;
    char link[STR_LINK_LEN];
    char page_link[STR_LINK_LEN];

public:
    /* The two URLs an item is asked for, built from its own fields rather than
     * from arguments the caller has to remember to pass. Both return false
     * when there is nothing to build or when the result did not fit, so a
     * doomed request is never made in the first place.
     *
     * const because they only read: it is what lets a caller hold the item by
     * const pointer and still address it. */
    bool stateUrl(char *out, size_t out_size) const;
    bool iconUrl(const char *website, char *out, size_t out_size) const;

    /* Fold the body of a "<link>/state" GET into state_text.
     *
     * The second half of update(), split out so that the half that waits on a
     * network and the half that decides what changed can run on different
     * tasks.
     *
     * @param len is explicit because a body straight off the network is not
     *   terminated. Over-long states are truncated, which is what
     *   HTTPClient::getString() plus strlcpy() did.
     * @return 1 if the state changed, 0 if it did not. There is no error
     *   return: a request that failed never reaches here.
     */
    int applyState(const char *text, size_t len);

    void cleanItem()
    {
        label[0] = 0;
        icon_name[0] = 0;
        type = type_unknown;
        state_text[0] = 0;
        transformedstate_text[0] = 0;
        link[0] = 0;
        /* Left behind before this line, which mattered because a widget only
         * assigns it when openHAB sends a "linkedPage": a Group rendered
         * inline, or a link that lost its target, kept the slot's page link
         * from the page before and navigated to it when tapped. */
        page_link[0] = 0;
        /* And the same for the mappings, which parse_selection() only writes
         * when the widget carries some: a Selection that arrives without them
         * offered the options of whatever last held the slot. */
        mapping_count = 0;
    }

    void setLabel(const char* newlabel) { strlcpy(label, newlabel, sizeof(label)); }
    const char* getLabel() { return label; }

    void setIconName(const char* newiconname) { strlcpy(icon_name, newiconname, sizeof(icon_name)); }
    const char* getIconName() { return icon_name; }

    void setLink(const char * newlink) { strlcpy(link, newlink, sizeof(link)); }
    const char * getLink() { return link; }
    bool hasLink() { return (strlen(link) > 0); }

    void setPageLink(const char * newlink) { strlcpy(page_link, newlink, sizeof(page_link)); }
    const char * getPageLink() { return page_link; }
    bool hasPageLink() { return (strlen(page_link) > 0); }

    enum ItemType getType() { return type; }
    void setType(enum ItemType newtype) { type = newtype; }

    const char* getStateText() { return state_text; }
    void setStateText(const char* newtext) { strlcpy(state_text, newtext, sizeof(state_text)); }

    const char* getTransformedStateText() { return transformedstate_text; }
    void setTransformedStateText(const char* newtranstext) { strlcpy(transformedstate_text, newtranstext, sizeof(transformedstate_text)); }

    float getStateNumber() { return strtof(state_text, NULL); }
    void setStateNumber(float newnumber) { snprintf(state_text, sizeof(state_text), "%f", newnumber); }

    /* A colorpicker's state, which openHAB sends as "h,s,v" -- degrees, then
     * two percents.
     *
     * Here rather than in the two UI files that draw a colour, because both of
     * them had a copy and both copies read past the end of the state: they ran
     * strtol() and then restarted at endptr + 1 without checking that endptr
     * was not already the terminator. The read stays inside state_text, which
     * is a fixed char[32], so this was never memory-unsafe -- it picked up
     * whatever digits a *previous, longer* state had left in the field. A
     * colorpicker showing "NULL", "UNDEF" or "" -- which is what openHAB sends
     * for an item it has no value for -- therefore got its colour out of the
     * item that last held the slot.
     *
     * @return false when the state is not three numbers, in which case the
     *   outputs are set to black rather than left undefined -- every caller
     *   paints something with them.
     */
    bool getStateHsv(uint16_t *h, uint8_t *s, uint8_t *v) const;

    bool stateIsNumber()
    {
        char * next;
        strtod(state_text, &next);
        return ((next != state_text) && (*next == '\0'));
    }

    const char* getNumberPattern() { return pattern; }
    void setNumberPattern(const char* newpattern) { strlcpy(pattern, newpattern, sizeof(pattern)); }
    bool hasNumberPattern() { return (strlen(pattern) > 1); }

    float getMinVal() { return min_val; }
    void setMinVal(float newval) { min_val = newval; }

    float getMaxVal() { return max_val; }
    void setMaxVal(float newval) { max_val = newval; }

    float getStep() { return step_val; }
    void setStep(float newval) { step_val = newval; }

    char *getSelectionCommand(size_t index) { return selection_command[index]; }
    void setSelectionCommand(size_t index, const char *command)
    {
        strlcpy(selection_command[index], command, sizeof(selection_command[index]));
    }
    void setSelectionLabel(size_t index, const char *label)
    {
        strlcpy(selection_label[index], label, sizeof(selection_label[index]));
    }
    char *getSelectionLabel(size_t index) { return selection_label[index]; }
    size_t getSelectionCount() { return mapping_count; }
    void setSelectionCount(size_t new_count) { mapping_count = new_count; }
};

/* How many of a server's sitemaps are offered for selection.
 *
 * A panel shows one sitemap and most servers have a handful, so this is a
 * bound on the list and not a limit anyone is expected to reach. It costs
 * SITEMAP_LIST_COUNT_MAX * (32 + 32) bytes of static storage in the one
 * SitemapList the firmware keeps, and a server with more than this says so:
 * getTotal() counts what arrived, getCount() what fitted, and both front ends
 * report the difference rather than silently showing the first twelve as if
 * they were all of them. The sitemap name is a text field in any case, so a
 * thirteenth is still reachable by typing it. */
#define SITEMAP_LIST_COUNT_MAX 12

/* One sitemap name, at the width Config stores it at. A server may serve a
 * longer one; the panel cannot be configured for it, so SitemapList::parse()
 * leaves it out of the list rather than offering a choice that would be
 * truncated on the way into config.json. The two widths are checked against
 * each other in openhab_sitemaps.cpp. */
#define STR_SITEMAP_NAME_LEN 32

/* The sitemaps a server offers, as GET /rest/sitemaps lists them.
 *
 * Next to Sitemap because it is the same kind of thing -- a response turned
 * into fixed-width fields, with no allocation and no pointers into the body --
 * and in this file for the same reason Sitemap is: it includes ArduinoJson and
 * libc and nothing else, so the host tests can reach it without a network.
 *
 * What the panel does with it is in openhab_sitemaps.cpp, which owns the one
 * instance, the fetch and the cache.
 */
class SitemapList
{
private:
    char   name[SITEMAP_LIST_COUNT_MAX][STR_SITEMAP_NAME_LEN];
    char   label[SITEMAP_LIST_COUNT_MAX][STR_LABEL_LEN];
    size_t count = 0;
    size_t total = 0;

public:
    /* Turn a /rest/sitemaps body into the list.
     *
     * `payload` need not be terminated -- a body off the network is not -- and
     * has to stay alive for the duration of the call, because ArduinoJson
     * parses in place. Nothing here ends up pointing into it.
     *
     * The response carries a "homepage" object per sitemap that this has no
     * use for, so the parse runs under a filter: only "name" and "label"
     * survive it, which keeps the document to a few dozen bytes per sitemap
     * however much the server sends.
     *
     * @return 0, or -1 for a body that did not parse or that is not the array
     *         of sitemaps this endpoint answers with. The list is emptied
     *         either way: a failed refresh must not leave the previous
     *         server's sitemaps on offer.
     */
    int parse(const char *payload, size_t payload_len);

    void clear();

    /* How many are on offer, and how many the server actually listed. They
     * differ when a server has more sitemaps than SITEMAP_LIST_COUNT_MAX, or
     * when one of them has a name too long for Config to hold. */
    size_t getCount() const { return count; }
    size_t getTotal() const { return total; }

    /* Both return "" rather than NULL for an index that is not there, so a
     * caller can print the result without testing it. The label is the
     * sitemap's own where it has one and its name where it has not -- openHAB
     * does not require a label and a blank row would name nothing. */
    const char *getName(size_t index) const;
    const char *getLabel(size_t index) const;
};

class Sitemap
{
private:
    char title[STR_TITLE_LEN];
    size_t item_count;
    Item item_array[ITEM_COUNT_MAX];

public:
    /* Turn a page already in memory into the title and the item array.
     *
     * `payload` need not be terminated -- a body off the network is not -- and
     * has to stay alive for the duration of the call, because ArduinoJson
     * parses in place. It does not have to survive the return: every field
     * extracted goes through one of Item's strlcpy() setters, so no Item ends
     * up holding a pointer into the page.
     *
     * @return 0, or -1 for a page that did not parse or that carries an
     *         openHAB error object instead of widgets.
     */
    int parse(const char *payload, size_t payload_len);

    const char* getPageName() { return title; }
    size_t getItemCount() { return item_count; }
    Item* getItem(size_t index) { return &item_array[index]; }
};

#endif
