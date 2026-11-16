/**
 * @file ha_ws.h
 *
 * A small WebSocket client (RFC 6455) for the Home Assistant API, over plain
 * BSD sockets.
 *
 * Small on purpose: the panel talks to one server, on the LAN, without TLS,
 * and Home Assistant sends text frames only. What is here:
 *
 * - the opening handshake (GET with Upgrade, answer 101),
 * - masked text frames out, in one frame each,
 * - text messages in, also when they span several continuation frames, read
 *   as a byte stream so that a parser can consume a message larger than any
 *   buffer the panel has,
 * - ping answered with pong, close reported as an error.
 *
 * Not here: TLS, extensions (no permessage-deflate is offered, so the server
 * sends none), binary messages (skipped), and the check of
 * Sec-WebSocket-Accept, which needs SHA-1 and protects against a confused
 * proxy rather than against an attacker.
 *
 * The socket is non-blocking and every wait is a select(): in the simulator,
 * any other blocking call stops the FreeRTOS scheduler (see
 * openhab_discover.cpp). The name lookup is the exception; a dotted address
 * skips it.
 *
 * The frame header functions are pure, so the host tests reach them.
 */
#ifndef HA_WS_H
#define HA_WS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Frame opcodes. */
#define HA_WS_OP_CONT  0x0
#define HA_WS_OP_TEXT  0x1
#define HA_WS_OP_BIN   0x2
#define HA_WS_OP_CLOSE 0x8
#define HA_WS_OP_PING  0x9
#define HA_WS_OP_PONG  0xA

/* The longest header a frame can have: 2 + 8 (length) + 4 (mask). */
#define HA_WS_HEADER_MAX 14

/* A decoded frame header. */
struct ha_ws_header_s
{
    bool     fin;
    uint8_t  opcode;
    bool     masked;
    uint8_t  mask[4];
    uint64_t length;      /* payload bytes */
    size_t   header_len;  /* bytes the header took */
};

/**
 * Encode a client frame header: FIN set, masked with `mask`.
 *
 * @return the header length, at most HA_WS_HEADER_MAX
 */
size_t ha_ws_encode_header(uint8_t *out, uint8_t opcode, uint64_t length,
                           const uint8_t mask[4]);

/**
 * Decode a frame header from the first `avail` bytes of `in`.
 *
 * @return 1 and `out` set, 0 if `avail` is too short for the whole header, -1
 *   for a header this client refuses (reserved bits set, a control frame
 *   longer than 125 bytes or fragmented).
 */
int ha_ws_decode_header(const uint8_t *in, size_t avail, struct ha_ws_header_s *out);

/* One connection. All fields are private to ha_ws.c. */
struct ha_ws_s
{
    int      fd;
    /* What is left of the current data frame's payload, and whether it is the
     * last frame of its message. */
    uint64_t frame_left;
    bool     frame_fin;
    bool     in_message;
    /* A small receive buffer, so that a header and the bytes after it come
     * in one recv(). */
    uint8_t  rx[256];
    size_t   rx_pos;
    size_t   rx_len;
    uint32_t timeout_ms;
};

/** Mark a connection as closed. Call once before the first connect. */
void ha_ws_init(struct ha_ws_s *ws);

/**
 * Open the TCP connection and do the handshake for `path` (for Home Assistant
 * "/api/websocket").
 *
 * @param timeout_ms for the connect, the handshake, and later for every wait
 *   inside one message
 * @return false on any failure; the connection is closed then
 */
bool ha_ws_connect(struct ha_ws_s *ws, const char *host, uint16_t port, const char *path,
                   uint32_t timeout_ms);

bool ha_ws_is_open(const struct ha_ws_s *ws);

void ha_ws_close(struct ha_ws_s *ws);

/** Send `len` bytes as one text frame. False closes the connection. */
bool ha_ws_send_text(struct ha_ws_s *ws, const char *text, size_t len);

/**
 * Wait up to `timeout_ms` for the start of the next text message, answering
 * pings and skipping binary messages on the way.
 *
 * Call only between messages: a message that was begun has to be read to its
 * end, or skipped with ha_ws_skip_message(), first.
 *
 * @return 1 when a message has begun, 0 when nothing arrived in time, -1 when
 *   the connection failed or the server closed it (the connection is closed
 *   then)
 */
int ha_ws_begin_message(struct ha_ws_s *ws, uint32_t timeout_ms);

/**
 * Read up to `len` payload bytes of the current message, across continuation
 * frames.
 *
 * @return the bytes read, 0 at the end of the message, -1 on failure (the
 *   connection is closed then)
 */
int ha_ws_read(struct ha_ws_s *ws, void *buf, size_t len);

/** Read and drop the rest of the current message. False on failure. */
bool ha_ws_skip_message(struct ha_ws_s *ws);

#ifdef __cplusplus
}
#endif

#endif /* HA_WS_H */
