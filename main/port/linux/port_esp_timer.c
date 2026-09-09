/**
 * @file linux/port_esp_timer.c
 *
 * esp_timer_get_time(), for the linux target only.
 *
 * Not a port in the sense the rest of this directory is -- nothing in the
 * application calls it. It is here because esp-mqtt does: components/mqtt
 * compiles cleanly for the linux target and its transport is all select()-based
 * (which is the one thing the FreeRTOS simulator wraps), so the *same* MQTT
 * client runs on the panel and in the simulator -- except that esp_timer
 * registers headers-only on linux, so its one call into it does not link.
 *
 * One function is a much smaller thing to supply than a second MQTT
 * implementation would be, and this is the only symbol missing: platform_random
 * resolves to esp_hw_support's port/linux/esp_random.c, and esp-mqtt's client-id
 * default needs no MAC because SOC_WIFI_SUPPORTED is not defined here.
 *
 * Should IDF ever give the linux target a real esp_timer, this becomes a
 * duplicate symbol -- which is a link error naming this file, not a silent
 * change of behaviour.
 */

#include "port_sys.h"

#include "esp_timer.h"

int64_t esp_timer_get_time(void)
{
    /* The same monotonic clock port_micros() reads, so a timestamp taken here
     * and one taken by the application cannot disagree about which came first.
     * esp_timer's contract is microseconds since boot, which is what
     * port_micros() measures. */
    return (int64_t)port_micros();
}
