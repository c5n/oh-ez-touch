/**
 * @file mdns_query.h
 *
 * One mDNS question, and the answer to it, as bytes.
 *
 * This is the whole of the DNS in "find the openHAB server": build a PTR
 * query for a service, and read the port and the name out of whatever comes
 * back. No responder, no cache, no service registration, no task -- the panel
 * asks twice in its life, when somebody is standing in front of the settings
 * screen, and it does not answer anybody else's questions.
 *
 * That is why there is no `espressif/mdns` component here. It would bring a
 * task, a cache and tens of kilobytes to make a query this file makes in a
 * fixed 44 bytes, and it builds for the device only -- the simulator would
 * need a stub, and the code that matters would then be the code no test and no
 * desktop run ever executes. What is here is libc and arithmetic, so it
 * compiles for the host tests and runs unchanged on both targets, with only
 * the socket above it (openhab_discover.cpp) knowing which is which.
 *
 * The one thing worth knowing about the wire format, because it is where a
 * hand-written parser goes wrong: a name may be a *pointer* to a name earlier
 * in the packet, and a real openHAB answer uses that three times over --
 * the SRV record's owner is a pointer into the middle of the PTR record's
 * data, which itself ends in a pointer to the question. Anything that reads a
 * name has to follow those, and has to refuse a packet whose pointers run
 * forwards or in a circle.
 */
#ifndef MDNS_QUERY_H
#define MDNS_QUERY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* An instance name as it is shown to somebody, which is the first label of
 * "<instance>.<service>" -- "openhab" out of
 * "openhab._openhab-server._tcp.local". */
#define MDNS_QUERY_INSTANCE_LEN 32

/* What a response may be, in bytes. 512 is the classic limit for DNS over UDP
 * and four times what a real openHAB answer to this question takes; a packet
 * larger than this is not read and not parsed, which costs a server that
 * padded its answer and nothing else. */
#define MDNS_QUERY_PACKET_MAX 512

struct mdns_answer_s
{
    char     instance[MDNS_QUERY_INSTANCE_LEN];
    uint16_t port;
};

/**
 * Build the question.
 *
 * @param service the full service name, without a trailing dot --
 *   "_openhab-server._tcp.local".
 * @param unicast_reply asks the responder to answer this packet directly
 *   instead of to the multicast group. It is what lets the caller use an
 *   ordinary ephemeral UDP socket: nothing has to join 224.0.0.251, nothing
 *   has to hold port 5353, and nothing is listening when no question is
 *   outstanding. openHAB honours it; a responder that does not will answer to
 *   the group, where this caller will simply not hear it.
 * @return the length written, or 0 if the name is malformed or does not fit.
 */
size_t mdns_query_build(uint8_t *buf, size_t size, const char *service, bool unicast_reply);

/**
 * Read one response.
 *
 * Looks for the first SRV record whose owner is an instance of `service`, and
 * takes the port from it and the display name from its owner. The *address* is
 * deliberately not taken from the packet: the datagram's source is by
 * definition an address that reaches the server, where the A record may carry
 * an interface the panel cannot route to -- so the caller pairs this port with
 * the address it received the packet from.
 *
 * A record with a TTL of zero is a responder announcing that the service is
 * going away, and is skipped rather than offered.
 *
 * @return false for a packet that is not a response, that carries no SRV for
 *   this service, or that is malformed in any way -- a truncated record, a
 *   length that runs past the end, a name that does not terminate, a
 *   compression pointer that runs forwards or loops. Nothing is written to
 *   `out` in that case.
 */
bool mdns_query_parse(const uint8_t *packet, size_t len, const char *service,
                      struct mdns_answer_s *out);

#ifdef __cplusplus
}
#endif

#endif /* MDNS_QUERY_H */
