/**
 * @file Arduino.cpp
 *
 * Implementation of the minimal Arduino compatibility layer, plus the `main()`
 * that drives the Arduino-style `setup()` / `loop()` of the simulator build.
 *
 * See Arduino.h for the rationale.
 */

#include "Arduino.h"

#include <chrono>
#include <thread>

SerialShim Serial;

static std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();

unsigned long millis(void)
{
    auto elapsed = std::chrono::steady_clock::now() - start_time;
    return (unsigned long)std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
}

unsigned long micros(void)
{
    auto elapsed = std::chrono::steady_clock::now() - start_time;
    return (unsigned long)std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
}

void delay(unsigned long ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

bool getLocalTime(struct tm *info, uint32_t ms)
{
    /* No NTP to wait for on the host: the clock is set or the process could not
     * have started. */
    (void)ms;

    time_t now = time(NULL);

    if (localtime_r(&now, info) == NULL)
        return false;

    return true;
}

void delayMicroseconds(unsigned int us)
{
    std::this_thread::sleep_for(std::chrono::microseconds(us));
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    start_time = std::chrono::steady_clock::now();

    setup();

    /* The SDL driver installs an SDL_QUIT handler that exits the process, so
     * closing the window terminates the simulator. */
    for (;;)
    {
        loop();
    }

    return 0;
}
