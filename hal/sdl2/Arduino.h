/**
 * @file Arduino.h
 *
 * Minimal Arduino compatibility layer for the native SDL2 simulator build
 * (`pio run -e linux`). Only the handful of Arduino APIs that the
 * simulator-reachable code actually touches is provided:
 *
 *   - `String`  : used by Config for the config file name
 *   - `Serial`  : used by debug.h and the DEBUG_* code paths
 *   - `millis()`: used by ui_infolabel and openhab_ui
 *   - `delay()`
 *   - `getLocalTime()`: used by openhab_ui for the header clock and the
 *     automatic night schedule
 *
 * Everything hardware related (WiFi, SPIFFS, TFT_eSPI, Ticker, ESP, ...) is
 * excluded from the simulator by the `SIMULATOR` guards in the sources and is
 * deliberately *not* stubbed here -- a missing symbol should be a build error,
 * not a silent no-op.
 */

#ifndef ARDUINO_H
#define ARDUINO_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <time.h>

/* strlcpy() is a BSD extension that the sources use for bounded, always
 * terminated copies. The ESP32 C library has it; glibc only from 2.38 on. */
#if defined(__GLIBC__) && !__GLIBC_PREREQ(2, 38)
static inline size_t strlcpy(char *dst, const char *src, size_t dst_size)
{
    size_t src_len = strlen(src);

    if (dst_size > 0)
    {
        size_t copy_len = (src_len < dst_size - 1) ? src_len : dst_size - 1;

        memcpy(dst, src, copy_len);
        dst[copy_len] = '\0';
    }

    return src_len;
}
#endif

/**********************
 *       String
 **********************/

class String
{
public:
    String() {}
    String(const char *s) : str(s ? s : "") {}
    String(const std::string &s) : str(s) {}

    const char *c_str() const { return str.c_str(); }
    size_t length() const { return str.length(); }
    bool isEmpty() const { return str.empty(); }

    String &operator=(const char *s)
    {
        str = s ? s : "";
        return *this;
    }

    String &operator+=(const String &other)
    {
        str += other.str;
        return *this;
    }

    bool operator==(const String &other) const { return str == other.str; }
    bool operator!=(const String &other) const { return str != other.str; }

    operator const char *() const { return str.c_str(); }

private:
    std::string str;
};

/**********************
 *       Serial
 **********************/

class SerialShim
{
public:
    /* Baudrate is meaningless on the host; accepted so debug_init() compiles. */
    void begin(unsigned long /*baudrate*/) {}
    void end() {}
    void flush() { fflush(stdout); }

    int printf(const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        int written = vfprintf(stdout, format, args);
        va_end(args);
        fflush(stdout);
        return written;
    }

    void print(const char *s) { fputs(s ? s : "", stdout); }
    void print(const String &s) { print(s.c_str()); }
    void print(char c) { fputc(c, stdout); }
    void print(int value) { fprintf(stdout, "%d", value); }
    void print(unsigned int value) { fprintf(stdout, "%u", value); }
    void print(long value) { fprintf(stdout, "%ld", value); }
    void print(unsigned long value) { fprintf(stdout, "%lu", value); }
    void print(double value) { fprintf(stdout, "%f", value); }

    void println() { fputc('\n', stdout); fflush(stdout); }

    template <typename T>
    void println(T value)
    {
        print(value);
        println();
    }
};

extern SerialShim Serial;

/**********************
 *        Time
 **********************/

/** Milliseconds since program start. */
unsigned long millis(void);

/** Microseconds since program start. */
unsigned long micros(void);

void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);

/** Local wall clock, the ESP32 core's signature.
 *
 * On the device this waits up to ms milliseconds for NTP to have set the clock
 * and fails while it has not. The host's clock is always set, so this fills in
 * from localtime() and succeeds -- which is the point: it lets the simulator
 * show a real time in the header and exercise the automatic night schedule,
 * neither of which could be seen otherwise. */
bool getLocalTime(struct tm *info, uint32_t ms = 5000);

/**********************
 *    Sketch entry
 **********************/

void setup(void);
void loop(void);

#endif /* ARDUINO_H */
