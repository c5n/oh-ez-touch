/**
 * @file port_ntp.h
 *
 * Setting the clock.
 *
 * Device: SNTP, plus the timezone the settings screen configures.
 * Host:   the operating system has already done both, so this only applies the
 *   configured timezone -- the simulator is meant to show what the panel would
 *   show, and that includes an offset the user got wrong.
 *
 * Reading the clock is port_localtime(), in port_sys.h, and its failure case is
 * the interesting half: it stays false until the clock is actually set.
 */
#ifndef PORT_NTP_H
#define PORT_NTP_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Apply the time configuration, and (re)start synchronisation where there is
 * any to do.
 *
 * Called at startup and again whenever the NTP settings change, which is why
 * it has to be safe to call repeatedly.
 *
 * @param server        NTP host name; ignored where the clock is not ours to set
 * @param gmt_offset_s  seconds east of UTC
 * @param dst_offset_s  additional seconds while daylight saving is in effect
 */
void port_ntp_setup(const char *server, int gmt_offset_s, int dst_offset_s);

#ifdef __cplusplus
}
#endif

#endif /* PORT_NTP_H */
