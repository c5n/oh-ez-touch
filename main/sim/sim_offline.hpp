/**
 * @file sim_offline.hpp
 *
 * Offline mode: serve openHAB from the compiled-in fixtures instead of the
 * network.
 *
 * This used to be what the simulator *was* -- there was no HTTP client on the
 * host at all, so sitemap_fixture.cpp and icon_fixture.cpp were the only source
 * of a page. The simulator makes real requests now, so the fixtures become a
 * choice: they still give a stable, reproducible screen for comparing
 * rendering changes, and they still let the UI be worked on with no openHAB
 * anywhere near.
 *
 * Off by default, so that the simulator behaves like the device unless told
 * otherwise.
 */
#ifndef SIM_OFFLINE_HPP
#define SIM_OFFLINE_HPP

/**
 * True when OHEZ_OFFLINE is set to anything but "0" or the empty string:
 *
 *   OHEZ_OFFLINE=1 ./build/linux/oh-ez-touch.elf
 *
 * Always false on the device, where there is no environment to read and no
 * fixture data compiled in to read it for.
 */
bool sim_offline(void);

#endif /* SIM_OFFLINE_HPP */
