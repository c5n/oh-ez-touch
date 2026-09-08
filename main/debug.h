
#ifndef DEBUG
#define DEBUG 0
#endif

#include <stdio.h>

/* Under ESP-IDF the console is up before app_main() runs -- the UART on the
 * device, the process's stdout on the host -- so there is nothing left to
 * initialise. Kept as a macro so the one call site in main.cpp still reads as
 * the start of the boot banner. */
#define debug_init()     {}

#define debug_printf(fmt, ...) \
            do { if (DEBUG) printf(fmt, ##__VA_ARGS__); } while (0)
