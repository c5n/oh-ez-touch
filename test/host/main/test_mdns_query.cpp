/* Unit tests for the mDNS question the panel asks to find an openHAB server.
 *
 * Two reasons this file exists rather than a trust in the protocol being
 * simple. The response is a packet a stranger on the network composed, and it
 * is parsed on the task that draws -- a name that never terminates, or a
 * compression pointer that points at itself, would hang the panel rather than
 * fail to find a server. And a real answer uses compression three times over,
 * so the half of this parser that is "skip a name" is the half that decides
 * whether the record after it is read from the right offset at all.
 *
 * The capture below is a real openHAB 5.2.1 answering this exact query on
 * 2026-09-18, byte for byte, which is what keeps these tests about the thing a
 * server sends rather than about the thing the parser expects.
 *
 * Run with:
 *   cd test/host && idf.py build && ./build/oh-ez-touch-host-test.elf
 */

#include <unity.h>

#include "net/mdns_query.h"
#include "test_suites.hpp"

#include <string.h>

#define SERVICE "_openhab-server._tcp.local"

/* GET the list of openHAB servers, as this firmware asks for it: one PTR
 * question with the unicast-response bit set. Captured from the wire. */
static const uint8_t query_expected[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0f, 0x5f, 0x6f, 0x70, 0x65, 0x6e, 0x68, 0x61, 0x62, 0x2d, 0x73, 0x65,
    0x72, 0x76, 0x65, 0x72, 0x04, 0x5f, 0x74, 0x63, 0x70, 0x05, 0x6c, 0x6f,
    0x63, 0x61, 0x6c, 0x00, 0x00, 0x0c, 0x80, 0x01,
};

/* And what came back: the question echoed, then PTR, A, TXT and SRV. The SRV
 * is last and its owner is a pointer into the middle of the PTR record's data,
 * which itself ends in a pointer to the question -- so reading it means
 * following two pointers, and reaching it means having skipped three records
 * whose names are pointers as well. */
static const uint8_t response_real[] = {
    0x00, 0x00, 0x84, 0x00, 0x00, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00,
    0x0f, 0x5f, 0x6f, 0x70, 0x65, 0x6e, 0x68, 0x61, 0x62, 0x2d, 0x73, 0x65,
    0x72, 0x76, 0x65, 0x72, 0x04, 0x5f, 0x74, 0x63, 0x70, 0x05, 0x6c, 0x6f,
    0x63, 0x61, 0x6c, 0x00, 0x00, 0x0c, 0x00, 0x01, 0xc0, 0x0c, 0x00, 0x0c,
    0x00, 0x01, 0x00, 0x00, 0x0e, 0x10, 0x00, 0x0a, 0x07, 0x6f, 0x70, 0x65,
    0x6e, 0x68, 0x61, 0x62, 0xc0, 0x0c, 0x0f, 0x31, 0x39, 0x32, 0x2d, 0x31,
    0x36, 0x38, 0x2d, 0x31, 0x33, 0x30, 0x2d, 0x31, 0x34, 0x39, 0xc0, 0x21,
    0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x0e, 0x10, 0x00, 0x04, 0xc0, 0xa8,
    0x82, 0x95, 0xc0, 0x38, 0x00, 0x10, 0x00, 0x01, 0x00, 0x00, 0x0e, 0x10,
    0x00, 0x0a, 0x09, 0x75, 0x72, 0x69, 0x3d, 0x2f, 0x72, 0x65, 0x73, 0x74,
    0xc0, 0x38, 0x00, 0x21, 0x00, 0x01, 0x00, 0x00, 0x0e, 0x10, 0x00, 0x08,
    0x00, 0x00, 0x00, 0x00, 0x1f, 0x90, 0xc0, 0x42,
};

/* ------------------------------------------------------------- a builder */

/* Enough to write the malformed packets below by hand without counting bytes.
 * Everything here writes forwards and nothing checks for room: a test that
 * overran would fail on the assertion, not silently. */
struct packet_s
{
    uint8_t bytes[256];
    size_t  len;
};

static void put8(struct packet_s *p, uint8_t value)
{
    p->bytes[p->len++] = value;
}

static void put16(struct packet_s *p, uint16_t value)
{
    put8(p, (uint8_t)(value >> 8));
    put8(p, (uint8_t)(value & 0xFF));
}

static void put32(struct packet_s *p, uint32_t value)
{
    put16(p, (uint16_t)(value >> 16));
    put16(p, (uint16_t)(value & 0xFFFF));
}

static void put_name(struct packet_s *p, const char *dotted)
{
    while (*dotted != '\0')
    {
        const char *dot = strchr(dotted, '.');
        size_t      label = (dot != NULL) ? (size_t)(dot - dotted) : strlen(dotted);

        put8(p, (uint8_t)label);
        memcpy(p->bytes + p->len, dotted, label);
        p->len += label;
        dotted += label + (dot != NULL ? 1 : 0);
    }

    put8(p, 0);
}

static void put_pointer(struct packet_s *p, uint16_t offset)
{
    put16(p, (uint16_t)(0xC000 | offset));
}

/* A response header with one answer and no question. */
static void put_header(struct packet_s *p, uint16_t answers)
{
    p->len = 0;
    put16(p, 0);      /* id      */
    put16(p, 0x8400); /* response, authoritative */
    put16(p, 0);      /* questions */
    put16(p, answers);
    put16(p, 0);
    put16(p, 0);
}

/* An SRV record for `owner`, carrying `port`.
 *
 * The data length is written after the data and patched back in rather than
 * counted by hand. The hand-counted version was wrong by one byte, which no
 * test with a single record could see -- and that is exactly the arithmetic
 * test_our_record_is_found_after_another() is checking the parser does. */
static void put_srv(struct packet_s *p, const char *owner, uint16_t port, uint32_t ttl)
{
    put_name(p, owner);
    put16(p, 33); /* SRV   */
    put16(p, 1);  /* IN    */
    put32(p, ttl);

    size_t rdlen_at = p->len;

    put16(p, 0);
    put16(p, 0); /* priority */
    put16(p, 0); /* weight   */
    put16(p, port);
    put_name(p, "host.local");

    size_t rdlen = p->len - rdlen_at - 2;

    p->bytes[rdlen_at] = (uint8_t)(rdlen >> 8);
    p->bytes[rdlen_at + 1] = (uint8_t)(rdlen & 0xFF);
}

/* -------------------------------------------------------------- the query */

static void test_the_query_is_the_one_on_the_wire(void)
{
    uint8_t buf[64];
    size_t  len = mdns_query_build(buf, sizeof(buf), SERVICE, true);

    TEST_ASSERT_EQUAL_UINT(sizeof(query_expected), len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(query_expected, buf, len);
}

/* Without the unicast bit the same question asks for an answer to the group,
 * which is the only difference between the two and is one bit of one byte. */
static void test_the_multicast_form_differs_in_one_bit(void)
{
    uint8_t buf[64];
    size_t  len = mdns_query_build(buf, sizeof(buf), SERVICE, false);

    TEST_ASSERT_EQUAL_UINT(sizeof(query_expected), len);
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[len - 2]);
    TEST_ASSERT_EQUAL_UINT8(0x01, buf[len - 1]);
}

static void test_a_query_that_does_not_fit_is_refused(void)
{
    uint8_t buf[20];

    TEST_ASSERT_EQUAL_UINT(0, mdns_query_build(buf, sizeof(buf), SERVICE, true));
}

static void test_a_malformed_service_name_is_refused(void)
{
    uint8_t buf[64];

    TEST_ASSERT_EQUAL_UINT(0, mdns_query_build(buf, sizeof(buf), "", true));
    TEST_ASSERT_EQUAL_UINT(0, mdns_query_build(buf, sizeof(buf), "_a..local", true));
    TEST_ASSERT_EQUAL_UINT(0, mdns_query_build(buf, sizeof(buf), ".local", true));
}

/* ----------------------------------------------------------- the response */

static void test_a_real_answer_yields_the_name_and_the_port(void)
{
    struct mdns_answer_s answer;

    TEST_ASSERT_TRUE(
        mdns_query_parse(response_real, sizeof(response_real), SERVICE, &answer));
    TEST_ASSERT_EQUAL_STRING("openhab", answer.instance);
    TEST_ASSERT_EQUAL_UINT16(8080, answer.port);
}

/* Every prefix of that packet, none of which may read past its end or claim to
 * have found a server. The full length is excluded -- that one is the test
 * above. */
static void test_every_truncation_of_it_is_refused(void)
{
    for (size_t len = 0; len < sizeof(response_real); len++)
    {
        struct mdns_answer_s answer;

        TEST_ASSERT_FALSE_MESSAGE(mdns_query_parse(response_real, len, SERVICE, &answer),
                                  "a truncated response was accepted");
    }
}

static void test_another_service_is_not_ours(void)
{
    struct mdns_answer_s answer;

    TEST_ASSERT_FALSE(
        mdns_query_parse(response_real, sizeof(response_real), "_printer._tcp.local", &answer));
}

/* Our own question, heard back off the multicast group. */
static void test_a_question_is_not_an_answer(void)
{
    uint8_t              packet[sizeof(query_expected)];
    struct mdns_answer_s answer;

    memcpy(packet, query_expected, sizeof(packet));
    TEST_ASSERT_FALSE(mdns_query_parse(packet, sizeof(packet), SERVICE, &answer));
}

static void test_a_packet_without_an_srv_is_refused(void)
{
    struct packet_s      p;
    struct mdns_answer_s answer;

    put_header(&p, 1);
    put_name(&p, SERVICE);
    put16(&p, 12); /* PTR */
    put16(&p, 1);
    put32(&p, 3600);
    put16(&p, 2);
    put_pointer(&p, 12);

    TEST_ASSERT_FALSE(mdns_query_parse(p.bytes, p.len, SERVICE, &answer));
}

/* A TTL of zero is a server announcing that it is going away. */
static void test_a_goodbye_is_not_an_offer(void)
{
    struct packet_s      p;
    struct mdns_answer_s answer;

    put_header(&p, 1);
    put_srv(&p, "openhab." SERVICE, 8080, 0);

    TEST_ASSERT_FALSE(mdns_query_parse(p.bytes, p.len, SERVICE, &answer));
}

/* Port 0 advertises nothing that can be asked for a sitemap. */
static void test_port_zero_is_refused(void)
{
    struct packet_s      p;
    struct mdns_answer_s answer;

    put_header(&p, 1);
    put_srv(&p, "openhab." SERVICE, 0, 3600);

    TEST_ASSERT_FALSE(mdns_query_parse(p.bytes, p.len, SERVICE, &answer));
}

/* The service is the tail of the owner name, so a service that merely ends the
 * same way is not this service: "._tcp.local" is shared by every service there
 * is. */
static void test_a_suffix_is_not_an_instance(void)
{
    struct packet_s      p;
    struct mdns_answer_s answer;

    put_header(&p, 1);
    put_srv(&p, "not-openhab-server._tcp.local", 8080, 3600);

    TEST_ASSERT_FALSE(mdns_query_parse(p.bytes, p.len, SERVICE, &answer));
}

/* A record for something else does not stop the record for us being found, and
 * that means the first one's length has to have been believed exactly. */
static void test_our_record_is_found_after_another(void)
{
    struct packet_s      p;
    struct mdns_answer_s answer;

    put_header(&p, 2);
    put_srv(&p, "brother._printer._tcp.local", 631, 3600);
    put_srv(&p, "attic." SERVICE, 8081, 3600);

    TEST_ASSERT_TRUE(mdns_query_parse(p.bytes, p.len, SERVICE, &answer));
    TEST_ASSERT_EQUAL_STRING("attic", answer.instance);
    TEST_ASSERT_EQUAL_UINT16(8081, answer.port);
}

/* --------------------------------------------------- what a stranger sends */

/* A name that points at itself. The parse must end rather than the panel. */
static void test_a_pointer_loop_is_refused(void)
{
    struct packet_s      p;
    struct mdns_answer_s answer;

    put_header(&p, 1);
    put_pointer(&p, 12); /* the record's own name, at offset 12 */
    put16(&p, 33);
    put16(&p, 1);
    put32(&p, 3600);
    put16(&p, 8);
    put32(&p, 0);
    put16(&p, 8080);
    put16(&p, 0);

    TEST_ASSERT_FALSE(mdns_query_parse(p.bytes, p.len, SERVICE, &answer));
}

/* And one that points forwards, which is the other way to make a reader loop
 * and is not legal compression either -- a pointer may only name something
 * already seen. */
static void test_a_forward_pointer_is_refused(void)
{
    struct packet_s      p;
    struct mdns_answer_s answer;

    put_header(&p, 1);
    put_pointer(&p, 40);
    put16(&p, 33);
    put16(&p, 1);
    put32(&p, 3600);
    put16(&p, 8);
    put32(&p, 0);
    put16(&p, 8080);
    put16(&p, 0);
    put_name(&p, "openhab." SERVICE);

    TEST_ASSERT_FALSE(mdns_query_parse(p.bytes, p.len, SERVICE, &answer));
}

/* A record whose data length reaches past the end of the packet. */
static void test_an_over_long_record_is_refused(void)
{
    struct packet_s      p;
    struct mdns_answer_s answer;

    put_header(&p, 1);
    put_name(&p, "openhab." SERVICE);
    put16(&p, 33);
    put16(&p, 1);
    put32(&p, 3600);
    put16(&p, 400); /* more than the packet holds */
    put16(&p, 0);
    put16(&p, 0);
    put16(&p, 8080);

    TEST_ASSERT_FALSE(mdns_query_parse(p.bytes, p.len, SERVICE, &answer));
}

/* A name that never terminates: label after label to the end of the buffer. */
static void test_a_name_that_never_ends_is_refused(void)
{
    struct packet_s      p;
    struct mdns_answer_s answer;

    put_header(&p, 1);

    while (p.len < sizeof(p.bytes) - 4)
    {
        put8(&p, 3);
        put8(&p, 'a');
        put8(&p, 'b');
        put8(&p, 'c');
    }

    TEST_ASSERT_FALSE(mdns_query_parse(p.bytes, p.len, SERVICE, &answer));
}

/* The header promises a record that is not there. */
static void test_a_promised_record_that_is_absent_is_refused(void)
{
    struct packet_s      p;
    struct mdns_answer_s answer;

    put_header(&p, 3);
    put_srv(&p, "openhab._other._tcp.local", 8080, 3600);

    TEST_ASSERT_FALSE(mdns_query_parse(p.bytes, p.len, SERVICE, &answer));
}

static void test_a_header_alone_is_refused(void)
{
    struct packet_s      p;
    struct mdns_answer_s answer;

    put_header(&p, 0);

    TEST_ASSERT_FALSE(mdns_query_parse(p.bytes, p.len, SERVICE, &answer));
}

void test_mdns_query_run(void)
{
    RUN_TEST(test_the_query_is_the_one_on_the_wire);
    RUN_TEST(test_the_multicast_form_differs_in_one_bit);
    RUN_TEST(test_a_query_that_does_not_fit_is_refused);
    RUN_TEST(test_a_malformed_service_name_is_refused);
    RUN_TEST(test_a_real_answer_yields_the_name_and_the_port);
    RUN_TEST(test_every_truncation_of_it_is_refused);
    RUN_TEST(test_another_service_is_not_ours);
    RUN_TEST(test_a_question_is_not_an_answer);
    RUN_TEST(test_a_packet_without_an_srv_is_refused);
    RUN_TEST(test_a_goodbye_is_not_an_offer);
    RUN_TEST(test_port_zero_is_refused);
    RUN_TEST(test_a_suffix_is_not_an_instance);
    RUN_TEST(test_our_record_is_found_after_another);
    RUN_TEST(test_a_pointer_loop_is_refused);
    RUN_TEST(test_a_forward_pointer_is_refused);
    RUN_TEST(test_an_over_long_record_is_refused);
    RUN_TEST(test_a_name_that_never_ends_is_refused);
    RUN_TEST(test_a_promised_record_that_is_absent_is_refused);
    RUN_TEST(test_a_header_alone_is_refused);
}
