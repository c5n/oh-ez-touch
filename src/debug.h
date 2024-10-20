
#ifndef DEBUG
#define DEBUG 0
#endif

#if 0

#define debug_init()     {}
#define debug_printf(fmt, ...) \
            do { if (DEBUG) fprintf(stderr, fmt, ##__VA_ARGS__); } while (0)

#else

#define debug_init()     Serial.begin(DEBUG_OUTPUT_BAUDRATE)
#define debug_printf(fmt, ...) \
            do { if (DEBUG) Serial.printf(fmt, ##__VA_ARGS__); } while (0)

#endif
