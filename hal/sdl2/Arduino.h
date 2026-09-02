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
 *
 * Everything hardware related (WiFi, SPIFFS, TFT_eSPI, Ticker, ESP, ...) is
 * excluded from the simulator by the `SIMULATOR` guards in the sources and is
 * deliberately *not* stubbed here -- a missing symbol should be a build error,
 * not a silent no-op.
 */

#ifndef ARDUINO_H
#define ARDUINO_H

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string>

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

/**********************
 *    Sketch entry
 **********************/

void setup(void);
void loop(void);

#endif /* ARDUINO_H */
