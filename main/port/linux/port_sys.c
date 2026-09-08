/**
 * @file linux/port_sys.c
 *
 * port_sys on the simulator host.
 */

#include "port_sys.h"

#include <malloc.h>
#include <stdlib.h>
#include <time.h>

#include "esp_log.h"

static const char *TAG = "port_sys";

/* clock_gettime(CLOCK_MONOTONIC) rather than esp_timer_get_time(): esp_timer
 * registers headers-only on the linux target, so calling it does not link.
 * This is measured from an arbitrary epoch rather than from boot, but nothing
 * here needs an absolute origin -- only differences and a monotonic direction. */
static uint64_t port_monotonic_us(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

uint64_t port_micros(void)
{
    static uint64_t origin = 0;

    if (origin == 0)
        origin = port_monotonic_us();

    return port_monotonic_us() - origin;
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
    /* exit() rather than re-exec: the simulator is started from a shell, and a
     * process that replaces itself would lose whatever the shell wrapped it in.
     * The persistent stores survive it -- config.json is a host file and NVS is
     * a host file -- so "restart and check the setting stuck" still works, it
     * just takes two commands. */
    ESP_LOGI(TAG, "restart requested; exiting");
    exit(0);
}

size_t port_free_heap(void)
{
    /* fordblks is the free space *inside* the arena, i.e. memory the allocator
     * holds but has not handed out. It is the closest thing the host has to the
     * device's figure, and it is still not the same thing: the host can always
     * ask the kernel for more. port_sys.h says not to make decisions on this. */
    struct mallinfo2 mi = mallinfo2();

    return (size_t)mi.fordblks;
}

bool port_localtime(struct tm *out)
{
    time_t now = time(NULL);

    if (localtime_r(&now, out) == NULL)
        return false;

    /* Always succeeds, unlike the device. The host clock is set by the OS
     * before this process starts, so there is no unsynchronised window to
     * report -- and the year check the device needs would reject nothing here
     * anyway. This is the behaviour hal/sdl2/Arduino.cpp already had. */
    return true;
}
