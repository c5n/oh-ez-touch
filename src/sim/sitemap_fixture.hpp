/**
 * @file sitemap_fixture.hpp
 *
 * Canned openHAB sitemap pages for the host simulator (SIMULATOR == 1).
 *
 * The simulator has no HTTP client -- every HTTPClient call in
 * openhab_connector.cpp is excluded by the SIMULATOR guards -- so the sitemap
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
 * Matching is done on the REST path (everything from "/rest/" onwards, with a
 * trailing "?type=json" removed), so the configured host and port do not
 * matter.
 *
 * @param url the URL the connector would have requested
 * @return the JSON body, or NULL if no fixture page matches
 */
const char *sim_sitemap_fixture_get(const char *url);

#endif /* SITEMAP_FIXTURE_HPP */
