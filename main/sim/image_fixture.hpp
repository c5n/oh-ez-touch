/**
 * @file image_fixture.hpp
 *
 * The picture offline mode shows for every Image item: a made-up front door,
 * the same JPEG doc/openHAB/seed-states.sh puts into the demo's doorbell.
 *
 * Compiled in on the linux target only. The panel has no offline mode to show
 * it in, and 14 KB of flash it has better uses for.
 */
#ifndef IMAGE_FIXTURE_HPP
#define IMAGE_FIXTURE_HPP

#include <stddef.h>

/** The JPEG, or NULL with *size 0 on the device. */
const unsigned char *sim_image_fixture_get(size_t *size);

#endif /* IMAGE_FIXTURE_HPP */
