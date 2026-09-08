/**
 * @file linux/port_ntp.c
 *
 * The host's clock is set by the operating system, so there is no
 * synchronisation to start. The timezone still applies: the simulator exists to
 * show what the panel would show, and a wrong GMT offset is one of the things
 * worth seeing before it ships.
 */
#include "port_ntp.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "esp_log.h"

static const char *TAG = "port_ntp";

void port_ntp_setup(const char *server, int gmt_offset_s, int dst_offset_s)
{
    (void)server;

    /* POSIX TZ counts west of UTC, the opposite way round from the setting,
     * hence the negated sign. The DST name is only ever selected by a rule,
     * and there is none here, so a non-zero DST offset means "always on" --
     * which is what the device's configTime() did with the same two numbers. */
    int total_s = gmt_offset_s + dst_offset_s;
    int west_s = -total_s;
    char tz[32];

    snprintf(tz, sizeof(tz), "OHEZ%+d:%02d:%02d",
             west_s / 3600, abs((west_s / 60) % 60), abs(west_s % 60));

    setenv("TZ", tz, 1);
    tzset();

    ESP_LOGI(TAG, "timezone %s (no NTP: the host clock is already set)", tz);
}
