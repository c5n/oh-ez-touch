/**
 * @file sitemap_fixture.hpp
 *
 * Canned openHAB sitemap pages for the host simulator.
 *
 * The simulator has no HTTP client yet -- every HTTPClient call in
 * openhab_connector.cpp is still excluded on the linux target -- so the sitemap
 * JSON is compiled in instead. This lets the UI be developed and reviewed on
 * the development machine without an openHAB server, and gives a stable,
 * reproducible screen for comparing rendering changes.
 *
 * The pages go through exactly the same parser as the responses from a real
 * server, so they also serve as a rough test vector for it.
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

#endif /* SITEMAP_FIXTURE_HPP */
