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
#include <string.h>
#include <time.h>

#include "esp_log.h"

static const char *TAG = "port_ntp";

void port_ntp_setup(const char *server, int gmt_offset_s, int dst_offset_s)
{
    (void)server;

    /* The same string the device builds, for the same reason: the simulator
     * exists to show what the panel would show, and what the panel shows is
     * the EU rule switching the saving on and off rather than a fixed offset.
     * See esp32/port_ntp.c for why the rule's hours move with the GMT
     * offset. */
    int  west_s = -gmt_offset_s;
    char tz[64];

    snprintf(tz, sizeof(tz), "OHEZ%+d:%02d:%02d",
             west_s / 3600, abs((west_s / 60) % 60), abs(west_s % 60));

    if (dst_offset_s > 0)
    {
        int west_dst_s = -(gmt_offset_s + dst_offset_s);
        int hours      = gmt_offset_s / 3600;
        int march      = hours + 1;
        int october    = hours + 2;

        if (march < 0)
            march = 0;
        if (october < 0)
            october = 0;

        size_t used = strlen(tz);

        snprintf(tz + used, sizeof(tz) - used,
                 "OHEZDST%+d:%02d:%02d,M3.5.0/%d,M10.5.0/%d",
                 west_dst_s / 3600, abs((west_dst_s / 60) % 60),
                 abs(west_dst_s % 60), march, october);
    }

    setenv("TZ", tz, 1);
    tzset();

    ESP_LOGI(TAG, "timezone %s (no NTP: the host clock is already set)", tz);
}
