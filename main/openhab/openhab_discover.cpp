/**
 * @file openhab_discover.cpp
 *
 * See openhab_discover.hpp.
 */

#include "sdkconfig.h"

#include "openhab_discover.hpp"

#include "net/mdns_query.h"
#include "port/port_sys.h"
#include "sim/sim_offline.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "esp_log.h"

static const char *TAG = "openhab_discover";

/* The service openHAB registers for its REST interface. There is a second one,
 * `_openhab-server-ssl._tcp`, on 8443; it is not asked for because this
 * firmware has no HTTPS client -- CONFIG_ESP_HTTP_CLIENT_ENABLE_HTTPS is off,
 * deliberately, and offering a server the panel cannot then reach would be
 * worse than not finding it. */
#define DISCOVER_SERVICE "_openhab-server._tcp.local"

#define DISCOVER_GROUP "224.0.0.251"
#define DISCOVER_PORT 5353

/* The multicast TTL mDNS asks for. Link-local traffic does not need 255 to
 * arrive, but a responder is entitled to check it. */
#define DISCOVER_TTL 255

/* How long answers are listened for, and when the question is asked a second
 * time. A real openHAB answered a unicast question in 56 to 70 ms on a wired
 * network; the window is long because multicast over WiFi is not, and the
 * repeat is there because a lost query is silent -- there is nothing to
 * retransmit and nobody to notice. Duplicate answers cost a string compare. */
#define DISCOVER_WINDOW_MS 2500
#define DISCOVER_REPEAT_MS 700

/* The widest address this stores. An IPv4 address in digits is fifteen
 * characters, and the field it will be copied into is Config's char[32]. */
#define DISCOVER_HOST_LEN 16

struct server_s
{
    char     label[MDNS_QUERY_INSTANCE_LEN];
    char     host[DISCOVER_HOST_LEN];
    uint16_t port;
};

static struct server_s servers[OPENHAB_DISCOVER_COUNT_MAX];
static size_t          server_count;

static enum openhab_discover_state_e state = OPENHAB_DISCOVER_IDLE;

/* Never zero, so that a front end can hold "the revision I last drew" in a
 * plain integer initialised to zero and be told about the first list it sees. */
static uint32_t revision = 1;

/* Written from any task, read and cleared by the loop. */
static bool pending;

static int      sock = -1;
static uint64_t window_deadline;
static uint64_t repeat_at;
static bool     repeated;

static void publish(enum openhab_discover_state_e new_state)
{
    state = new_state;
    revision++;

    if (revision == 0)
        revision = 1;
}

void openhab_discover_request(void)
{
    pending = true;
}

/* Keep one entry per address and port. A server answers the question twice
 * because it is asked twice, and a panel with both radios up may even hear the
 * same answer on two interfaces. */
static void server_insert(const char *host, uint16_t port, const char *label)
{
    for (size_t i = 0; i < server_count; i++)
    {
        if (servers[i].port == port && strcmp(servers[i].host, host) == 0)
            return;
    }

    if (server_count >= OPENHAB_DISCOVER_COUNT_MAX)
        return;

    strlcpy(servers[server_count].host, host, sizeof(servers[server_count].host));
    strlcpy(servers[server_count].label, label, sizeof(servers[server_count].label));
    servers[server_count].port = port;
    server_count++;
}

static void socket_close(void)
{
    if (sock < 0)
        return;

    close(sock);
    sock = -1;
}

/* One question onto the wire.
 *
 * Built every time rather than kept: it is 44 bytes of stack against a static
 * buffer and a flag saying whether it has been filled in. */
static bool query_send(void)
{
    uint8_t query[64];
    size_t  len = mdns_query_build(query, sizeof(query), DISCOVER_SERVICE, true);

    if (len == 0)
    {
        ESP_LOGE(TAG, "cannot build a query for %s", DISCOVER_SERVICE);
        return false;
    }

    struct sockaddr_in group;

    memset(&group, 0, sizeof(group));
    group.sin_family = AF_INET;
    group.sin_port = htons(DISCOVER_PORT);
    group.sin_addr.s_addr = inet_addr(DISCOVER_GROUP);

    if (sendto(sock, query, len, 0, (struct sockaddr *)&group, sizeof(group)) < 0)
    {
        ESP_LOGW(TAG, "sendto %s: %s", DISCOVER_GROUP, strerror(errno));
        return false;
    }

    return true;
}

/* Answer from a fixture instead of the network.
 *
 * The same bargain the sitemap pages strike in offline mode: a stable screen
 * to work on with no server anywhere. The address is the loopback one a
 * developer's own openHAB is usually on, so picking it is not a dead end. */
static void discover_offline(void)
{
    server_insert("127.0.0.1", 8080, "openhab");
    publish(OPENHAB_DISCOVER_READY);
}

static void scan_start(void)
{
    server_count = 0;

    if (sim_offline() == true)
    {
        discover_offline();
        return;
    }

    socket_close();

    sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock < 0)
    {
        ESP_LOGE(TAG, "socket: %s", strerror(errno));
        publish(OPENHAB_DISCOVER_FAILED);
        return;
    }

    /* Non-blocking is not an optimisation, and the comment in testif.cpp is
     * the same one: every blocking call but select() parks the FreeRTOS POSIX
     * scheduler on the host, and this is read from the task that draws. */
    if (fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK) != 0)
    {
        ESP_LOGE(TAG, "O_NONBLOCK: %s", strerror(errno));
        socket_close();
        publish(OPENHAB_DISCOVER_FAILED);
        return;
    }

    /* Not fatal if it is refused: 255 is what the specification asks for, and
     * a link-local datagram arrives at a hop count of one anyway. */
    int ttl = DISCOVER_TTL;

    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) != 0)
        ESP_LOGD(TAG, "IP_MULTICAST_TTL: %s", strerror(errno));

    /* No bind, no group membership and no port 5353. The question asks for a
     * direct answer, so it comes back to the ephemeral port the first sendto()
     * assigns -- which is also what keeps this socket from hearing the rest of
     * the network's mDNS chatter. */
    if (query_send() == false)
    {
        socket_close();
        publish(OPENHAB_DISCOVER_FAILED);
        return;
    }

    window_deadline = port_millis() + DISCOVER_WINDOW_MS;
    repeat_at = port_millis() + DISCOVER_REPEAT_MS;
    repeated = false;

    publish(OPENHAB_DISCOVER_SCANNING);
}

/* Take whatever has arrived. Bounded by the socket's own buffer rather than by
 * a count: every packet is one answer to one question this panel asked, and
 * there are as many as there are servers. */
static void answers_drain(void)
{
    for (;;)
    {
        uint8_t            packet[MDNS_QUERY_PACKET_MAX];
        struct sockaddr_in from;
        socklen_t          from_len = sizeof(from);

        ssize_t len = recvfrom(sock, packet, sizeof(packet), 0, (struct sockaddr *)&from,
                               &from_len);

        if (len <= 0)
            return; /* EAGAIN, which is the usual answer */

        struct mdns_answer_s answer;

        if (mdns_query_parse(packet, (size_t)len, DISCOVER_SERVICE, &answer) == false)
            continue;

        char host[DISCOVER_HOST_LEN];
        uint32_t addr = ntohl(from.sin_addr.s_addr);

        /* By hand rather than through inet_ntoa(), which returns a pointer
         * into static storage on one target and is a macro over a different
         * one on the other. */
        snprintf(host, sizeof(host), "%u.%u.%u.%u", (unsigned)((addr >> 24) & 0xFF),
                 (unsigned)((addr >> 16) & 0xFF), (unsigned)((addr >> 8) & 0xFF),
                 (unsigned)(addr & 0xFF));

        server_insert(host, answer.port, answer.instance);

        ESP_LOGI(TAG, "%s at %s:%u", answer.instance, host, (unsigned)answer.port);
    }
}

void openhab_discover_loop(void)
{
    if (pending == true && state != OPENHAB_DISCOVER_SCANNING)
    {
        pending = false;
        scan_start();
        return;
    }

    if (state != OPENHAB_DISCOVER_SCANNING || sock < 0)
    {
        /* A want that arrived mid-scan is dropped here rather than queued: by
         * the time this one closes, its answers are the answer. */
        pending = false;
        return;
    }

    answers_drain();

    if (repeated == false && port_millis() >= repeat_at)
    {
        repeated = true;
        query_send();
    }

    if (port_millis() < window_deadline)
        return;

    socket_close();

    /* READY with nothing in the list is not a failure: it is the answer
     * "nobody announced themselves", which is a real and common state -- a
     * server behind an access point that drops multicast looks exactly like
     * this. FAILED is kept for the question that could not be asked at all. */
    publish(OPENHAB_DISCOVER_READY);
}

enum openhab_discover_state_e openhab_discover_state(void)
{
    /* A want that has been recorded but not yet acted on is already a scan as
     * far as anyone reading this is concerned -- the same reason
     * openhab_sitemaps_state() reports one. */
    if (pending == true)
        return OPENHAB_DISCOVER_SCANNING;

    return state;
}

size_t openhab_discover_count(void)
{
    return server_count;
}

const char *openhab_discover_host(size_t index)
{
    return (index < server_count) ? servers[index].host : "";
}

uint16_t openhab_discover_port(size_t index)
{
    return (index < server_count) ? servers[index].port : 0;
}

const char *openhab_discover_label(size_t index)
{
    return (index < server_count) ? servers[index].label : "";
}

uint32_t openhab_discover_revision(void)
{
    return revision;
}
