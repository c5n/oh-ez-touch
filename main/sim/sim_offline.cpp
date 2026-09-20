/**
 * @file sim_offline.cpp
 *
 * See sim_offline.hpp.
 */

#include "sdkconfig.h"

#include "sim_offline.hpp"

#if CONFIG_IDF_TARGET_LINUX

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

bool sim_offline(void)
{
    /* Read once. The answer cannot change while the process runs, and every
     * openHAB request asks. */
    static int cached = -1;

    if (cached < 0)
    {
        const char *value = getenv("OHEZ_OFFLINE");

        cached = (value != NULL && value[0] != '\0' && strcmp(value, "0") != 0) ? 1 : 0;

        if (cached != 0)
            ESP_LOGI("sim_offline", "offline mode: serving the compiled-in fixtures");
    }

    return cached != 0;
}

#else /* !CONFIG_IDF_TARGET_LINUX */

bool sim_offline(void)
{
    return false;
}

#endif
