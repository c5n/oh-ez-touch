/**
 * @file esp32/port_sys.c
 *
 * port_sys on the device.
 */

#include "port_sys.h"

#include <stdlib.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

static const char *TAG = "port_sys";

/* esp_timer_get_time() is the microsecond monotonic clock the whole SDK uses,
 * and it survives a light sleep. The host uses clock_gettime(CLOCK_MONOTONIC)
 * instead because esp_timer registers headers-only on the linux target, so
 * calling it there is a link error rather than a runtime surprise. */
uint64_t port_micros(void)
{
    return (uint64_t)esp_timer_get_time();
}

uint64_t port_millis(void)
{
    return port_micros() / 1000ULL;
}

uint32_t port_tick_ms(void)
{
    return (uint32_t)port_millis();
}

void port_restart(void)
{
    esp_restart();
}

size_t port_free_heap(void)
{
    return (size_t)esp_get_free_heap_size();
}

size_t port_largest_free_block(void)
{
    /* Internal 8-bit DRAM, which is what every malloc() in this firmware is
     * served from: there is no PSRAM on any of these boards, and the display's
     * DMA buffers come out of the same pool through heap_caps_malloc(). */
    return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
}

void port_heap_info(struct port_heap_info_s *out)
{
    /* The same pool the two accessors above answer for. */
    multi_heap_info_t info;

    heap_caps_get_info(&info, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);

    out->free          = info.total_free_bytes;
    out->largest       = info.largest_free_block;
    out->min_free_ever = info.minimum_free_bytes;
    out->alloc_blocks  = info.allocated_blocks;
    out->free_blocks   = info.free_blocks;
}

bool port_localtime(struct tm *out)
{
    time_t now = 0;

    time(&now);

    if (localtime_r(&now, out) == NULL)
        return false;

    /* The failure this exists for. Before NTP has synchronised the clock reads
     * some time in 1970, and openhab_ui.cpp relies on being told so: it keeps
     * the theme variant currently in effect rather than deciding that 01:00 on
     * 1 January 1970 falls inside the configured night window. Any year this
     * firmware could plausibly have been built in is a good enough cut-off. */
    if (out->tm_year + 1900 < 2020)
    {
        ESP_LOGD(TAG, "wall clock not synchronised yet");
        return false;
    }

    return true;
}
