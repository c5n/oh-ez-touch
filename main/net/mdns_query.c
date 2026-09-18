/**
 * @file mdns_query.c
 *
 * See mdns_query.h.
 */

#include "mdns_query.h"

#include <string.h>
#include <strings.h> /* strcasecmp() */

#define MDNS_HEADER_LEN 12

#define MDNS_TYPE_SRV 33
#define MDNS_CLASS_IN 0x0001
#define MDNS_CLASS_MASK 0x7FFF /* the top bit is cache-flush in a response */

#define MDNS_QTYPE_PTR 12
#define MDNS_QU_BIT 0x8000 /* "answer me directly, not to the group" */

#define MDNS_FLAG_RESPONSE 0x8000

/* A name is at most 255 bytes of wire format, and every label costs a length
 * byte, so the dotted form is always shorter than this. */
#define MDNS_NAME_MAX 256

/* How many compression pointers one name may follow. Every jump must also go
 * strictly backwards, so this is belt and braces -- but a packet is a thing a
 * stranger on the network wrote, and a parser that can be made to loop by one
 * is a parser that can hang the panel. */
#define MDNS_NAME_JUMPS_MAX 8

static uint16_t read16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* Read the name at `off` into `out` as dotted text, and say where the record
 * continues.
 *
 * `out` may be NULL to skip a name rather than keep it. `*next` is where the
 * *first* label sequence ended -- following a pointer does not advance the
 * record, which is the whole point of one.
 *
 * Returns false for anything malformed, including a name too long for `out`:
 * this is compared against a service name, and a truncated comparison would
 * silently accept the wrong service.
 */
static bool name_read(const uint8_t *packet, size_t len, size_t off, char *out, size_t out_size,
                      size_t *next)
{
    size_t   written = 0;
    unsigned jumps = 0;
    bool     jumped = false;

    if (out != NULL && out_size > 0)
        out[0] = '\0';

    for (;;)
    {
        if (off >= len)
            return false;

        uint8_t length = packet[off];

        if ((length & 0xC0) == 0xC0)
        {
            if (off + 1 >= len)
                return false;

            size_t target = ((size_t)(length & 0x3F) << 8) | packet[off + 1];

            if (jumped == false && next != NULL)
                *next = off + 2;

            jumped = true;

            /* Backwards only. A pointer to itself or to anything later is the
             * one shape that makes this loop. */
            if (target >= off || ++jumps > MDNS_NAME_JUMPS_MAX)
                return false;

            off = target;
            continue;
        }

        /* 0x40 and 0x80 are label types nothing sends and nobody has to
         * support; treating them as a length would read the packet out of
         * step. */
        if ((length & 0xC0) != 0)
            return false;

        off++;

        if (length == 0)
            break;

        if (off + length > len)
            return false;

        if (out != NULL)
        {
            /* The dot, the label and the terminator have to fit. */
            if (written + (written > 0 ? 1u : 0u) + length + 1u > out_size)
                return false;

            if (written > 0)
                out[written++] = '.';

            memcpy(out + written, packet + off, length);
            written += length;
            out[written] = '\0';
        }

        off += length;
    }

    if (jumped == false && next != NULL)
        *next = off;

    return true;
}

/* Whether `name` is "<something>.<service>", which is what an instance of a
 * service is called. Case-insensitive, because DNS names are. */
static bool name_is_instance_of(const char *name, const char *service)
{
    size_t name_len = strlen(name);
    size_t service_len = strlen(service);

    if (name_len <= service_len + 1)
        return false;

    if (name[name_len - service_len - 1] != '.')
        return false;

    return (strcasecmp(name + name_len - service_len, service) == 0);
}

static size_t name_encode(uint8_t *buf, size_t size, const char *name)
{
    size_t written = 0;

    while (*name != '\0')
    {
        const char *dot = strchr(name, '.');
        size_t      label = (dot != NULL) ? (size_t)(dot - name) : strlen(name);

        /* An empty label is a name with ".." in it or a leading dot, and 63 is
         * the widest a label can be. */
        if (label == 0 || label > 63)
            return 0;

        if (written + 1 + label + 1 > size)
            return 0;

        buf[written++] = (uint8_t)label;
        memcpy(buf + written, name, label);
        written += label;

        name += label;

        if (*name == '.')
            name++;
    }

    if (written == 0 || written + 1 > size)
        return 0;

    buf[written++] = 0;

    return written;
}

size_t mdns_query_build(uint8_t *buf, size_t size, const char *service, bool unicast_reply)
{
    if (buf == NULL || service == NULL || size < MDNS_HEADER_LEN)
        return 0;

    /* Transaction id 0: a responder echoes it and nothing here reads it back.
     * mDNS proper ignores the id entirely, and the one thing that could use it
     * -- telling two outstanding questions apart -- is not a thing this asks. */
    memset(buf, 0, MDNS_HEADER_LEN);
    buf[4] = 0;
    buf[5] = 1; /* one question */

    size_t name = name_encode(buf + MDNS_HEADER_LEN, size - MDNS_HEADER_LEN, service);

    if (name == 0)
        return 0;

    size_t off = MDNS_HEADER_LEN + name;

    if (off + 4 > size)
        return 0;

    buf[off++] = 0;
    buf[off++] = MDNS_QTYPE_PTR;

    uint16_t qclass = MDNS_CLASS_IN | (unicast_reply ? MDNS_QU_BIT : 0);

    buf[off++] = (uint8_t)(qclass >> 8);
    buf[off++] = (uint8_t)(qclass & 0xFF);

    return off;
}

bool mdns_query_parse(const uint8_t *packet, size_t len, const char *service,
                      struct mdns_answer_s *out)
{
    if (packet == NULL || service == NULL || out == NULL || len < MDNS_HEADER_LEN)
        return false;

    /* Our own question, seen on the multicast group. Not an error and not
     * worth a log line -- it simply is not an answer. */
    if ((read16(packet + 2) & MDNS_FLAG_RESPONSE) == 0)
        return false;

    uint16_t questions = read16(packet + 4);
    uint32_t records = (uint32_t)read16(packet + 6) + read16(packet + 8) + read16(packet + 10);

    size_t off = MDNS_HEADER_LEN;

    /* The question comes back with the answer when a responder is replying to
     * a unicast query, so it is skipped rather than assumed absent. */
    for (uint16_t i = 0; i < questions; i++)
    {
        if (name_read(packet, len, off, NULL, 0, &off) == false)
            return false;

        if (off + 4 > len)
            return false;

        off += 4;
    }

    for (uint32_t i = 0; i < records; i++)
    {
        char owner[MDNS_NAME_MAX];

        /* An owner name too long for the buffer fails the whole packet rather
         * than the record: at that point the parse is out of step with the
         * record boundaries and everything after it is a guess. */
        if (name_read(packet, len, off, owner, sizeof(owner), &off) == false)
            return false;

        if (off + 10 > len)
            return false;

        uint16_t type = read16(packet + off);
        uint16_t rclass = read16(packet + off + 2);
        uint32_t ttl = read32(packet + off + 4);
        uint16_t rdlen = read16(packet + off + 8);

        off += 10;

        if (off + rdlen > len)
            return false;

        /* SRV is priority, weight, port, target -- six bytes before the name,
         * and the name is of no use here: the address this answer came from is
         * what the panel can reach. */
        if (   type == MDNS_TYPE_SRV
            && (rclass & MDNS_CLASS_MASK) == MDNS_CLASS_IN
            && ttl != 0
            && rdlen >= 6
            && name_is_instance_of(owner, service) == true)
        {
            size_t label = strcspn(owner, ".");

            if (label >= sizeof(out->instance))
                label = sizeof(out->instance) - 1;

            memcpy(out->instance, owner, label);
            out->instance[label] = '\0';
            out->port = read16(packet + off + 4);

            /* A server that answered with port 0 is advertising nothing
             * usable, and the caller would build "http://host:0/" out of it. */
            return (out->port != 0);
        }

        off += rdlen;
    }

    return false;
}
