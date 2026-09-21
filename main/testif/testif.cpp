/**
 * @file testif.cpp
 *
 * The socket and the command table. See testif.hpp.
 */

#include "sdkconfig.h"

#include "testif.hpp"

#if CONFIG_IDF_TARGET_LINUX

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "esp_log.h"

#include "port_display.h"
#include "port_sys.h"

#include "testif_internal.hpp"

static const char *TAG = "testif";

/* One past the web server's 8780. Adjacent on purpose: the two are the panel's
 * two doors on the host, and remembering one means remembering the other. */
#define TESTIF_DEFAULT_PORT 8781

/* The reply is one datagram. Loopback carries 64 KB of them, so this is a
 * formatting budget rather than a transport limit -- the `screen` dump of a
 * full page is about 1.5 KB and this leaves room for a page with long labels. */
#define TESTIF_REPLY_MAX 4096

static int sock = -1;

/* Set by `quit`, acted on in testif_loop() once the reply has gone out -- an
 * exit() from inside the handler would take the process down with the answer
 * still unsent, and a harness would see a timeout instead of an "ok". */
static bool quit_requested = false;

struct testif_route_s
{
    const char *verb;
    testif_fn_t fn;
};

/* `quit` is not in the table: it has to answer before it exits, which no
 * handler returning a reason can do. It is handled in dispatch(). */
static const struct testif_route_s routes[] = {
    { "tap",       testif_cmd_tap },
    { "longpress", testif_cmd_longpress },
    { "swipe",     testif_cmd_swipe },
    { "press",     testif_cmd_press },
    { "move",      testif_cmd_move },
    { "release",   testif_cmd_release },
    { "screen",    testif_cmd_screen },
    { "status",    testif_cmd_status },
    { "config",    testif_cmd_config },
    { "set",       testif_cmd_set },
    { "nav",       testif_cmd_nav },
    { "settings",  testif_cmd_settings },
    { "calibrate", testif_cmd_calibrate },
    { "shot",      testif_cmd_shot },
};

/* ------------------------------------------------------------ shared bits */

bool testif_arg_int(const testif_cmd_t *cmd, unsigned index, long *out)
{
    if (index >= cmd->argc)
        return false;

    const char *text = cmd->argv[index];
    char       *end  = NULL;

    long value = strtol(text, &end, 10);

    /* The whole token or nothing: "12x" is a typo, not a 12. */
    if (end == text || *end != '\0')
        return false;

    *out = value;

    return true;
}

const char *testif_coords(const testif_cmd_t *cmd, unsigned first, int32_t *x, int32_t *y)
{
    long vx = 0;
    long vy = 0;

    if (testif_arg_int(cmd, first, &vx) == false ||
        testif_arg_int(cmd, first + 1, &vy) == false)
        return "want x y";

    /* The live resolution, not PORT_DISPLAY_WIDTH/HEIGHT: those are the
     * landscape axes, and a portrait panel's valid x range ends at 240 while
     * its y range runs to 320. */
    if (vx < 0 || vx >= lv_display_get_horizontal_resolution(NULL) ||
        vy < 0 || vy >= lv_display_get_vertical_resolution(NULL))
        return "range";

    *x = (int32_t)vx;
    *y = (int32_t)vy;

    return NULL;
}

/* --------------------------------------------------------------- dispatch */

static size_t dispatch(char *request, char *reply, size_t reply_size)
{
    testif_cmd_t cmd;

    if (testif_parse(request, &cmd) == false)
        return (size_t)snprintf(reply, reply_size, "err empty\n");

    /* The id is echoed on every answer, refusals included, so a client that
     * timed out on an earlier request can recognise its late reply instead of
     * reading it as the answer to the current one.
     *
     * Static, like the two buffers below and for the same reason: this runs on
     * the task that also draws the screen, and `nav` and `settings` reach from
     * here into LVGL calls that build a whole screen. Ten kilobytes of reply
     * buffer under those would be ten kilobytes they no longer have. Safe
     * because there is one caller and it is never re-entered. */
    static char prefix[TESTIF_LINE_MAX];

    if (cmd.id != NULL)
        snprintf(prefix, sizeof(prefix), "@%s ", cmd.id);
    else
        prefix[0] = '\0';

    if (cmd.truncated == true)
        return (size_t)snprintf(reply, reply_size, "%serr too many arguments\n", prefix);

    if (strcmp(cmd.argv[0], "ping") == 0)
        return (size_t)snprintf(reply, reply_size, "%sok %llu\n", prefix,
                                (unsigned long long)port_millis());

    if (strcmp(cmd.argv[0], "quit") == 0)
    {
        int n = snprintf(reply, reply_size, "%sok\n", prefix);

        quit_requested = true;

        return (size_t)n;
    }

    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++)
    {
        if (strcmp(cmd.argv[0], routes[i].verb) != 0)
            continue;

        /* Payload and reason are kept apart so a handler cannot half-write an
         * answer and then fail: what it left in `payload` is used only when it
         * reports success. */
        static char payload[TESTIF_REPLY_MAX];

        payload[0] = '\0';

        const char *reason = routes[i].fn(&cmd, payload, sizeof(payload));

        if (reason != NULL)
            return (size_t)snprintf(reply, reply_size, "%serr %s\n", prefix, reason);

        if (payload[0] == '\0')
            return (size_t)snprintf(reply, reply_size, "%sok\n", prefix);

        return (size_t)snprintf(reply, reply_size, "%sok %s\n", prefix, payload);
    }

    return (size_t)snprintf(reply, reply_size, "%serr unknown command\n", prefix);
}

/* ------------------------------------------------------------------ setup */

void testif_setup(void)
{
    const char *enabled = getenv("OHEZ_TESTIF");

    if (enabled != NULL && (strcmp(enabled, "0") == 0 || enabled[0] == '\0'))
    {
        ESP_LOGI(TAG, "disabled by OHEZ_TESTIF");
        return;
    }

    uint16_t    port = TESTIF_DEFAULT_PORT;
    const char *text = getenv("OHEZ_TESTIF_PORT");

    if (text != NULL && text[0] != '\0')
        port = (uint16_t)atoi(text);

    sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock < 0)
    {
        ESP_LOGE(TAG, "socket: %s", strerror(errno));
        return;
    }

    /* Non-blocking is not an optimisation here. Every blocking call but
     * select() parks the FreeRTOS POSIX scheduler, and this one is made from
     * the task that draws the screen -- a blocking recvfrom() would stop the
     * simulator dead until a datagram arrived. */
    if (fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK) != 0)
    {
        ESP_LOGE(TAG, "O_NONBLOCK: %s", strerror(errno));
        close(sock);
        sock = -1;
        return;
    }

    struct sockaddr_in addr;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        /* Not fatal. A second simulator, or something else already on the
         * port, should cost the interface and not the run. */
        ESP_LOGW(TAG, "bind to 127.0.0.1:%u: %s -- no control interface",
                 (unsigned)port, strerror(errno));
        close(sock);
        sock = -1;
        return;
    }

    testif_touch_init();
    testif_shot_init();

    ESP_LOGI(TAG, "listening on 127.0.0.1:%u", (unsigned)port);
}

void testif_loop(void)
{
    if (sock < 0)
        return;

    /* One request per turn of ohez_loop(), not a drain loop: a client that
     * floods the socket would otherwise hold the UI task for as long as it
     * kept sending. At a 5 ms loop that is still 200 commands a second. */
    char               request[TESTIF_LINE_MAX];
    struct sockaddr_in from;
    socklen_t          from_len = sizeof(from);

    ssize_t n = recvfrom(sock, request, sizeof(request) - 1, 0,
                         (struct sockaddr *)&from, &from_len);

    if (n < 0)
        return; /* EAGAIN, which is the usual answer */

    request[n] = '\0';

    static char reply[TESTIF_REPLY_MAX + TESTIF_LINE_MAX];

    size_t len = dispatch(request, reply, sizeof(reply));

    if (len > sizeof(reply) - 1)
        len = sizeof(reply) - 1;

    sendto(sock, reply, len, 0, (struct sockaddr *)&from, from_len);

    if (quit_requested == true)
    {
        ESP_LOGI(TAG, "quit");
        exit(0);
    }
}

#else /* !CONFIG_IDF_TARGET_LINUX */

void testif_setup(void) {}
void testif_loop(void) {}
bool testif_frame_hold(void) { return false; }

#endif
