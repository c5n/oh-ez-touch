/**
 * @file ohez_port.h
 *
 * The platform boundary. Everything the application needs from the platform is
 * declared by one of the headers included below, and each of them has exactly
 * two implementations -- main/port/esp32/ and main/port/linux/ -- selected by
 * main/CMakeLists.txt.
 *
 * This replaces the `#if (SIMULATOR != 1)` guards that were scattered through
 * src/ and the `build_src_filter` subtractions in platformio.ini. Two rules
 * follow from that, and they are the whole point of the layer:
 *
 *   1. A concern that only one target can implement gets a header of its own,
 *      so the application says *what* it needs and the target says how.
 *   2. A missing symbol is a build error, not a silent no-op. Where a target
 *      genuinely has nothing to do -- the simulator has no backlight -- the
 *      implementation is an explicit, documented no-op, never an empty stub
 *      standing in for something observable.
 *
 * Ports that arrive with later commits: port_ota.
 */
#ifndef OHEZ_PORT_H
#define OHEZ_PORT_H

#include "port_backlight.h"
#include "port_beeper.h"
#include "port_display.h"
#include "port_indev.h"
#include "port_kv.h"
#include "port_net.h"
#include "port_ntp.h"
#include "port_storage.h"
#include "port_sys.h"

#endif /* OHEZ_PORT_H */
