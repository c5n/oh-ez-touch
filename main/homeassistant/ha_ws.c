/**
 * @file ha_ws.c
 *
 * See ha_ws.h.
 */
#include "ha_ws.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "esp_log.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

static const char *TAG = "ha_ws";

/* --------------------------------------------------------- frame headers */

size_t ha_ws_encode_header(uint8_t *out, uint8_t opcode, uint64_t length,
                           const uint8_t mask[4])
{
    size_t n = 0;

    out[n++] = (uint8_t)(0x80u | (opcode & 0x0Fu));

    if (length < 126)
    {
        out[n++] = (uint8_t)(0x80u | length);
    }
    else if (length <= 0xFFFFu)
    {
        out[n++] = 0x80u | 126u;
        out[n++] = (uint8_t)(length >> 8);
        out[n++] = (uint8_t)length;
    }
    else
    {
        out[n++] = 0x80u | 127u;

        for (int shift = 56; shift >= 0; shift -= 8)
            out[n++] = (uint8_t)(length >> shift);
    }

    memcpy(&out[n], mask, 4);
    n += 4;

    return n;
}

int ha_ws_decode_header(const uint8_t *in, size_t avail, struct ha_ws_header_s *out)
{
    if (avail < 2)
        return 0;

    /* RSV1..3: no extension was negotiated, so none may be set. */
    if ((in[0] & 0x70u) != 0)
        return -1;

    out->fin    = (in[0] & 0x80u) != 0;
    out->opcode = in[0] & 0x0Fu;
    out->masked = (in[1] & 0x80u) != 0;

    uint8_t len7 = in[1] & 0x7Fu;
    size_t  n    = 2;

    if (len7 == 126)
    {
        if (avail < n + 2)
            return 0;

        out->length = ((uint64_t)in[2] << 8) | in[3];
        n += 2;
    }
    else if (len7 == 127)
    {
        if (avail < n + 8)
            return 0;

        out->length = 0;

        for (int i = 0; i < 8; i++)
            out->length = (out->length << 8) | in[2 + i];

        n += 8;
    }
    else
    {
        out->length = len7;
    }

    if (out->masked)
    {
        if (avail < n + 4)
            return 0;

        memcpy(out->mask, &in[n], 4);
        n += 4;
    }
    else
    {
        memset(out->mask, 0, 4);
    }

    /* Control frames: at most 125 bytes, never fragmented (RFC 6455 5.5). */
    if ((out->opcode & 0x08u) != 0 && (out->length > 125 || out->fin == false))
        return -1;

    out->header_len = n;

    return 1;
}

/* ------------------------------------------------------------ the socket */

void ha_ws_init(struct ha_ws_s *ws)
{
    memset(ws, 0, sizeof(*ws));
    ws->fd = -1;
}

bool ha_ws_is_open(const struct ha_ws_s *ws)
{
    return ws->fd >= 0;
}

void ha_ws_close(struct ha_ws_s *ws)
{
    if (ws->fd >= 0)
        close(ws->fd);

    ha_ws_init(ws);
}

/* select() for one direction. 1 ready, 0 timeout, -1 error. */
static int wait_fd(int fd, bool for_write, uint32_t timeout_ms)
{
    fd_set set;
    struct timeval tv;

    FD_ZERO(&set);
    FD_SET(fd, &set);
    tv.tv_sec  = (long)(timeout_ms / 1000u);
    tv.tv_usec = (long)((timeout_ms % 1000u) * 1000u);

    int rc = select(fd + 1, for_write ? NULL : &set, for_write ? &set : NULL, NULL, &tv);

    if (rc < 0 && errno == EINTR)
        return 0;

    return (rc > 0) ? 1 : rc;
}

static bool send_all(struct ha_ws_s *ws, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;

    while (len > 0)
    {
        ssize_t sent = send(ws->fd, p, len, MSG_NOSIGNAL);

        if (sent > 0)
        {
            p += sent;
            len -= (size_t)sent;
            continue;
        }

        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        {
            if (wait_fd(ws->fd, true, ws->timeout_ms) <= 0)
                return false;

            continue;
        }

        return false;
    }

    return true;
}

/* Make at least one byte available in rx. False on timeout, error or EOF. */
static bool fill(struct ha_ws_s *ws, uint32_t timeout_ms)
{
    if (ws->rx_pos < ws->rx_len)
        return true;

    ws->rx_pos = 0;
    ws->rx_len = 0;

    for (;;)
    {
        ssize_t got = recv(ws->fd, ws->rx, sizeof(ws->rx), 0);

        if (got > 0)
        {
            ws->rx_len = (size_t)got;
            return true;
        }

        if (got == 0)
            return false; /* the server closed the TCP connection */

        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            return false;

        if (wait_fd(ws->fd, false, timeout_ms) <= 0)
            return false;
    }
}

/* Exactly `len` bytes, within the connection's timeout. */
static bool read_exact(struct ha_ws_s *ws, void *dst, size_t len)
{
    uint8_t *out = (uint8_t *)dst;

    while (len > 0)
    {
        if (fill(ws, ws->timeout_ms) == false)
            return false;

        size_t n = ws->rx_len - ws->rx_pos;

        if (n > len)
            n = len;

        memcpy(out, &ws->rx[ws->rx_pos], n);
        ws->rx_pos += n;
        out += n;
        len -= n;
    }

    return true;
}

static bool read_header(struct ha_ws_s *ws, struct ha_ws_header_s *h)
{
    uint8_t buf[HA_WS_HEADER_MAX];
    size_t  have = 2;

    if (read_exact(ws, buf, 2) == false)
        return false;

    for (;;)
    {
        int rc = ha_ws_decode_header(buf, have, h);

        if (rc == 1)
            return true;

        if (rc < 0 || have >= sizeof(buf))
        {
            ESP_LOGW(TAG, "refused frame header %02x %02x", buf[0], buf[1]);
            return false;
        }

        if (read_exact(ws, &buf[have], 1) == false)
            return false;

        have++;
    }
}

static void random_bytes(uint8_t *out, size_t len)
{
    static bool seeded;

    if (seeded == false)
    {
        srand((unsigned)time(NULL) ^ (unsigned)(uintptr_t)out);
        seeded = true;
    }

    for (size_t i = 0; i < len; i++)
        out[i] = (uint8_t)(rand() >> 7);
}

static bool send_frame(struct ha_ws_s *ws, uint8_t opcode, const void *data, size_t len)
{
    uint8_t mask[4];
    uint8_t header[HA_WS_HEADER_MAX];
    uint8_t chunk[128];

    random_bytes(mask, sizeof(mask));

    size_t hlen = ha_ws_encode_header(header, opcode, len, mask);

    if (send_all(ws, header, hlen) == false)
        return false;

    /* Masked in chunks rather than in a copy of the whole payload: a page's
     * subscription names a dozen entities and is the longest thing sent. */
    const uint8_t *p = (const uint8_t *)data;

    for (size_t done = 0; done < len;)
    {
        size_t n = len - done;

        if (n > sizeof(chunk))
            n = sizeof(chunk);

        for (size_t i = 0; i < n; i++)
            chunk[i] = p[done + i] ^ mask[(done + i) & 3u];

        if (send_all(ws, chunk, n) == false)
            return false;

        done += n;
    }

    return true;
}

static void base64_encode(const uint8_t *in, size_t len, char *out)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;

    for (size_t i = 0; i < len; i += 3)
    {
        uint32_t v = (uint32_t)in[i] << 16;

        if (i + 1 < len)
            v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < len)
            v |= in[i + 2];

        out[o++] = alphabet[(v >> 18) & 63u];
        out[o++] = alphabet[(v >> 12) & 63u];
        out[o++] = (i + 1 < len) ? alphabet[(v >> 6) & 63u] : '=';
        out[o++] = (i + 2 < len) ? alphabet[v & 63u] : '=';
    }

    out[o] = '\0';
}

static bool open_socket(struct ha_ws_s *ws, const char *host, uint16_t port)
{
    struct sockaddr_in addr;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);

    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1)
    {
        /* Blocking, and the one blocking call here: there is no non-blocking
         * resolver on both targets. A dotted address does not get here. */
        struct addrinfo hints;
        struct addrinfo *res = NULL;

        memset(&hints, 0, sizeof(hints));
        hints.ai_family   = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        if (getaddrinfo(host, NULL, &hints, &res) != 0 || res == NULL)
        {
            ESP_LOGW(TAG, "cannot resolve \"%s\"", host);
            return false;
        }

        addr.sin_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
        freeaddrinfo(res);
    }

    ws->fd = socket(AF_INET, SOCK_STREAM, 0);

    if (ws->fd < 0)
        return false;

    if (fcntl(ws->fd, F_SETFL, fcntl(ws->fd, F_GETFL, 0) | O_NONBLOCK) != 0)
        return false;

    int one = 1;

    setsockopt(ws->fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    if (connect(ws->fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        if (errno != EINPROGRESS)
            return false;

        if (wait_fd(ws->fd, true, ws->timeout_ms) <= 0)
            return false;

        int       err = 0;
        socklen_t err_len = sizeof(err);

        if (getsockopt(ws->fd, SOL_SOCKET, SO_ERROR, &err, &err_len) != 0 || err != 0)
            return false;
    }

    return true;
}

bool ha_ws_connect(struct ha_ws_s *ws, const char *host, uint16_t port, const char *path,
                   uint32_t timeout_ms)
{
    ha_ws_close(ws);
    ws->timeout_ms = timeout_ms;

    if (open_socket(ws, host, port) == false)
    {
        ESP_LOGW(TAG, "cannot connect to %s:%u", host, (unsigned)port);
        ha_ws_close(ws);
        return false;
    }

    uint8_t key_raw[16];
    char    key[25];
    char    request[256];

    random_bytes(key_raw, sizeof(key_raw));
    base64_encode(key_raw, sizeof(key_raw), key);

    int len = snprintf(request, sizeof(request),
                       "GET %s HTTP/1.1\r\n"
                       "Host: %s:%u\r\n"
                       "Upgrade: websocket\r\n"
                       "Connection: Upgrade\r\n"
                       "Sec-WebSocket-Key: %s\r\n"
                       "Sec-WebSocket-Version: 13\r\n"
                       "\r\n",
                       path, host, (unsigned)port, key);

    if (len <= 0 || (size_t)len >= sizeof(request) || send_all(ws, request, (size_t)len) == false)
    {
        ha_ws_close(ws);
        return false;
    }

    /* The response head, up to the blank line. What follows it stays in rx:
     * the server may send its first frame in the same segment. */
    char   head[512];
    size_t head_len = 0;

    for (;;)
    {
        if (fill(ws, ws->timeout_ms) == false || head_len >= sizeof(head) - 1)
        {
            ESP_LOGW(TAG, "no handshake answer from %s:%u", host, (unsigned)port);
            ha_ws_close(ws);
            return false;
        }

        head[head_len++] = (char)ws->rx[ws->rx_pos++];

        if (head_len >= 4 && memcmp(&head[head_len - 4], "\r\n\r\n", 4) == 0)
            break;
    }

    head[head_len] = '\0';

    if (strncmp(head, "HTTP/1.1 101", 12) != 0)
    {
        char *eol = strchr(head, '\r');

        if (eol != NULL)
            *eol = '\0';

        ESP_LOGW(TAG, "handshake refused: %s", head);
        ha_ws_close(ws);
        return false;
    }

    return true;
}

bool ha_ws_send_text(struct ha_ws_s *ws, const char *text, size_t len)
{
    if (ws->fd < 0)
        return false;

    if (send_frame(ws, HA_WS_OP_TEXT, text, len) == false)
    {
        ha_ws_close(ws);
        return false;
    }

    return true;
}

/* A control frame whose header has been read: answer a ping, drop a pong,
 * report a close. False when the connection is over. */
static bool handle_control(struct ha_ws_s *ws, const struct ha_ws_header_s *h)
{
    uint8_t payload[125];
    size_t  len = (size_t)h->length;

    if (read_exact(ws, payload, len) == false)
        return false;

    if (h->opcode == HA_WS_OP_PING)
        return send_frame(ws, HA_WS_OP_PONG, payload, len);

    if (h->opcode == HA_WS_OP_CLOSE)
    {
        ESP_LOGW(TAG, "server closed the connection");
        return false;
    }

    return true;
}

int ha_ws_begin_message(struct ha_ws_s *ws, uint32_t timeout_ms)
{
    if (ws->fd < 0)
        return -1;

    for (;;)
    {
        /* The only wait that may run out without an error: nothing arrived. */
        if (ws->rx_pos >= ws->rx_len)
        {
            int rc = wait_fd(ws->fd, false, timeout_ms);

            if (rc == 0)
                return 0;

            if (rc < 0 || fill(ws, ws->timeout_ms) == false)
                break;
        }

        struct ha_ws_header_s h;

        if (read_header(ws, &h) == false)
            break;

        if ((h.opcode & 0x08u) != 0)
        {
            if (handle_control(ws, &h) == false)
                break;

            continue;
        }

        if (h.opcode == HA_WS_OP_TEXT)
        {
            ws->frame_left = h.length;
            ws->frame_fin  = h.fin;
            ws->in_message = true;
            return 1;
        }

        if (h.opcode == HA_WS_OP_BIN)
        {
            ws->frame_left = h.length;
            ws->frame_fin  = h.fin;
            ws->in_message = true;

            if (ha_ws_skip_message(ws) == false)
                break;

            continue;
        }

        /* A continuation with no message begun. */
        ESP_LOGW(TAG, "stray frame, opcode %u", (unsigned)h.opcode);
        break;
    }

    ha_ws_close(ws);
    return -1;
}

int ha_ws_read(struct ha_ws_s *ws, void *buf, size_t len)
{
    if (ws->fd < 0)
        return -1;

    if (ws->in_message == false || len == 0)
        return 0;

    while (ws->frame_left == 0)
    {
        if (ws->frame_fin == true)
        {
            ws->in_message = false;
            return 0;
        }

        struct ha_ws_header_s h;

        if (read_header(ws, &h) == false)
            goto fail;

        if ((h.opcode & 0x08u) != 0)
        {
            if (handle_control(ws, &h) == false)
                goto fail;

            continue;
        }

        if (h.opcode != HA_WS_OP_CONT)
            goto fail;

        ws->frame_left = h.length;
        ws->frame_fin  = h.fin;
    }

    if (fill(ws, ws->timeout_ms) == false)
        goto fail;

    size_t n = ws->rx_len - ws->rx_pos;

    if (n > len)
        n = len;
    if ((uint64_t)n > ws->frame_left)
        n = (size_t)ws->frame_left;

    memcpy(buf, &ws->rx[ws->rx_pos], n);
    ws->rx_pos += n;
    ws->frame_left -= n;

    return (int)n;

fail:
    ha_ws_close(ws);
    return -1;
}

bool ha_ws_skip_message(struct ha_ws_s *ws)
{
    uint8_t scratch[64];

    for (;;)
    {
        int n = ha_ws_read(ws, scratch, sizeof(scratch));

        if (n == 0)
            return true;

        if (n < 0)
            return false;
    }
}
