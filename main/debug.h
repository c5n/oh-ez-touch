#ifndef DEBUG_H
#define DEBUG_H

/* The debug switches, from Kconfig: "OhEzTouch -> Debug output" in
 * `idf.py menuconfig`. They were -D flags in one PlatformIO environment's
 * forty-line build_flags block, which is why nineteen of them were only ever
 * set in the JTAG environment and the rest were never set at all.
 *
 * Included here because every module that tests one of them includes this
 * header: an unset Kconfig bool is simply undefined, so a file that tested one
 * without sdkconfig.h in scope would read it as off however it was
 * configured. */
#include "sdkconfig.h"

#include <stdio.h>

/* An unset Kconfig bool is undefined rather than 0, and the runtime test in
 * debug_printf() below needs an expression either way. */
#ifndef CONFIG_OHEZ_DEBUG
#define CONFIG_OHEZ_DEBUG 0
#endif

/* Under ESP-IDF the console is up before app_main() runs -- the UART on the
 * device, the process's stdout on the host -- so there is nothing left to
 * initialise. Kept as a macro so the one call site in main.cpp still reads as
 * the start of the boot banner. */
#define debug_init()     {}

#define debug_printf(fmt, ...) \
            do { if (CONFIG_OHEZ_DEBUG) printf(fmt, ##__VA_ARGS__); } while (0)

#endif /* DEBUG_H */
