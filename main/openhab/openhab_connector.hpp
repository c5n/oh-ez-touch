/**
 * @file openhab_connector.hpp
 *
 * The openHAB side of the page model: the list of sitemaps a server offers.
 * The model itself (Item, Sitemap) is shared and lives in
 * model/page_model.hpp; Sitemap::parse() and the Item URL members are
 * defined in openhab_connector.cpp.
 */
#ifndef OPENHAB_CONNECTOR_HPP
#define OPENHAB_CONNECTOR_HPP

#include <ArduinoJson.h>

#include "model/page_model.hpp"
#include "openhab_color.h"

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

#endif /* OPENHAB_CONNECTOR_HPP */
