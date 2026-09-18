/**
 * @file sitemap_fixture.hpp
 *
 * Canned openHAB sitemap pages for the host simulator.
 *
 * The simulator makes real requests these days, so these are a choice rather
 * than a necessity: OHEZ_OFFLINE=1 serves them instead of going to the
 * network, which gives a stable, reproducible screen for comparing rendering
 * changes and lets the UI be worked on with no openHAB anywhere near. The
 * openHAB client task decides between the two, so nothing above it can tell.
 *
 * The pages go through exactly the same parser as the responses from a real
 * server, which is why test/host feeds them to Sitemap::parse() directly: a
 * change that breaks the parser breaks the tests and this screen together,
 * rather than one without the other.
 */

#ifndef SITEMAP_FIXTURE_HPP
#define SITEMAP_FIXTURE_HPP

/**
 * Look up a canned sitemap page.
 *
 * Matching is done on the page name in /rest/sitemaps/<sitemap>/<page>, with
 * the page whose name equals the sitemap's taken as the home page -- so neither
 * the configured host and port nor the configured sitemap name has to match the
 * fixture.
 *
 * @param url the URL the connector would have requested
 * @return the JSON body, or NULL if no fixture page matches
 */
const char *sim_sitemap_fixture_get(const char *url);

/**
 * The canned answer to GET /rest/sitemaps, for the settings screen's and the
 * web form's list of sitemaps to choose from.
 *
 * Shaped like a real server's -- checked against openHAB 5.2.1 -- which means
 * a "homepage" object per entry that SitemapList::parse() filters out. It
 * lists the one sitemap the pages above make up plus two the fixtures do not
 * have, so that offline mode shows a list worth looking at and picking one
 * that has no fixture behind it behaves the way picking a sitemap a server
 * does not serve behaves.
 *
 * @return the JSON body, or NULL on the device, which has a real server to ask
 */
const char *sim_sitemap_fixture_list(void);

#endif /* SITEMAP_FIXTURE_HPP */
