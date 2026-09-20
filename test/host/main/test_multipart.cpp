/* Unit tests for the multipart scanner in main/web/multipart.c.
 *
 * This is the body of the firmware upload, and it is worth more coverage than
 * its size suggests for two reasons. It is the one path in this firmware that
 * can leave a panel unbootable, and it is the only one that parses bytes
 * somebody else chose with no authentication in front of it -- /update is
 * open, and the setup access point, which is also open, reaches it.
 *
 * It was untestable until it was split out of webui_ota.cpp: it called
 * esp_ota_begin() and esp_ota_write() directly, which made the whole file
 * device-only. It writes into a sink now, and the sink here is a buffer.
 *
 * The interesting cases are all about the boundary, because the boundary is
 * the part the sender controls: its length, whether it appears inside the
 * payload, and whether the payload ends on a prefix of it.
 */

#include <unity.h>

#include <stdio.h>
#include <string.h>

#include "test_suites.hpp"
#include "web/multipart.h"

/* Bigger than any payload below. */
#define SINK_MAX 4096

struct sink_s
{
    char   data[SINK_MAX];
    size_t len;
    int    begins;
    bool   refuse_begin;
    bool   refuse_write;
};

static bool sink_begin(void *ctx)
{
    struct sink_s *s = (struct sink_s *)ctx;

    s->begins++;

    return (s->refuse_begin == false);
}

static bool sink_write(void *ctx, const char *data, size_t len)
{
    struct sink_s *s = (struct sink_s *)ctx;

    if (s->refuse_write == true)
        return false;

    TEST_ASSERT_TRUE_MESSAGE(s->len + len <= SINK_MAX, "sink overflow");

    memcpy(s->data + s->len, data, len);
    s->len += len;

    return true;
}

/* Assemble a body the way curl does and run it through in one block. */
static void run(struct multipart_s *mp, struct sink_s *sink, const char *boundary,
                const char *payload, size_t payload_len, bool terminate)
{
    char   body[SINK_MAX * 2];
    size_t n = 0;

    memset(sink, 0, sizeof(*sink));
    multipart_init(mp, sink_begin, sink_write, sink);

    n += (size_t)snprintf(body + n, sizeof(body) - n,
                          "%s\r\nContent-Disposition: form-data; name=\"image\"; "
                          "filename=\"fw.bin\"\r\nContent-Type: "
                          "application/octet-stream\r\n\r\n",
                          boundary);

    memcpy(body + n, payload, payload_len);
    n += payload_len;

    if (terminate == true)
        n += (size_t)snprintf(body + n, sizeof(body) - n, "\r\n%s--\r\n", boundary);

    multipart_feed_block(mp, body, n);
}

/* ------------------------------------------------------------------ tests */

/* The ordinary case: one part, delivered whole. */
static void test_a_normal_upload(void)
{
    struct multipart_s mp;
    struct sink_s      sink;
    const char        *payload = "\xE9\x01\x02\x03 firmware bytes \x00\xFF here";
    size_t             len = 34;

    run(&mp, &sink, "------------------------abc123", payload, len, true);

    TEST_ASSERT_TRUE(multipart_complete(&mp));
    TEST_ASSERT_FALSE(multipart_failed(&mp));
    TEST_ASSERT_EQUAL_INT(1, sink.begins);
    TEST_ASSERT_EQUAL_size_t(len, sink.len);
    TEST_ASSERT_EQUAL_MEMORY(payload, sink.data, len);
}

/* Delivered a byte at a time, which is what a slow client on a shared radio
 * looks like: the scanner is a state machine precisely so the chunk
 * boundaries cannot matter. */
static void test_chunking_does_not_matter(void)
{
    static const char *const boundary = "--xyz";
    const char               payload[] = "0123456789abcdefghij";
    size_t                   len = sizeof(payload) - 1;

    char   body[512];
    size_t n = (size_t)snprintf(body, sizeof(body), "%s\r\nContent-Type: x\r\n\r\n", boundary);

    memcpy(body + n, payload, len);
    n += len;
    n += (size_t)snprintf(body + n, sizeof(body) - n, "\r\n%s--\r\n", boundary);

    /* Every chunk size from 1 up to the whole body. */
    for (size_t step = 1; step <= n; step++)
    {
        struct multipart_s mp;
        struct sink_s      sink;

        memset(&sink, 0, sizeof(sink));
        multipart_init(&mp, sink_begin, sink_write, &sink);

        for (size_t off = 0; off < n; off += step)
        {
            size_t take = (off + step <= n) ? step : (n - off);

            multipart_feed_block(&mp, body + off, take);
        }

        TEST_ASSERT_TRUE(multipart_complete(&mp));
        TEST_ASSERT_EQUAL_size_t(len, sink.len);
        TEST_ASSERT_EQUAL_MEMORY(payload, sink.data, len);
    }
}

/* A boundary longer than the field it is copied into.
 *
 * This is the one that mattered: the copy took head_len + 1 bytes into a
 * boundary eight bytes narrower than head, so a first line of 80 characters
 * or more overwrote boundary_len -- and boundary_len is what bounds the hold
 * buffer in the data state, so the next few hundred kilobytes went wherever
 * the sender's bytes said. It has to be refused, not truncated. */
static void test_an_over_long_boundary_is_refused(void)
{
    struct multipart_s mp;
    struct sink_s      sink;
    char               boundary[MULTIPART_BOUNDARY_MAX * 2];

    memset(boundary, '-', sizeof(boundary) - 1);
    boundary[sizeof(boundary) - 1] = '\0';

    run(&mp, &sink, boundary, "payload", 7, true);

    TEST_ASSERT_TRUE(multipart_failed(&mp));
    TEST_ASSERT_FALSE(multipart_complete(&mp));

    /* And nothing was opened or written on the way to refusing it. */
    TEST_ASSERT_EQUAL_INT(0, sink.begins);
    TEST_ASSERT_EQUAL_size_t(0, sink.len);
}

/* The longest boundary that does fit still works, which is what says the
 * bound is off by nothing. */
static void test_the_longest_allowed_boundary_works(void)
{
    struct multipart_s mp;
    struct sink_s      sink;
    char               boundary[MULTIPART_BOUNDARY_MAX];

    memset(boundary, '-', sizeof(boundary) - 1);
    boundary[sizeof(boundary) - 1] = '\0';

    run(&mp, &sink, boundary, "payload", 7, true);

    TEST_ASSERT_TRUE(multipart_complete(&mp));
    TEST_ASSERT_EQUAL_size_t(7, sink.len);
    TEST_ASSERT_EQUAL_MEMORY("payload", sink.data, 7);
}

/* An empty first line names a boundary that matches everywhere, so the scan
 * would end on the first CRLF of the image. */
static void test_an_empty_boundary_is_refused(void)
{
    struct multipart_s mp;
    struct sink_s      sink;

    memset(&sink, 0, sizeof(sink));
    multipart_init(&mp, sink_begin, sink_write, &sink);

    multipart_feed_block(&mp, "\r\nContent-Type: x\r\n\r\npayload\r\n--\r\n", 34);

    TEST_ASSERT_TRUE(multipart_failed(&mp));
    TEST_ASSERT_EQUAL_size_t(0, sink.len);
}

/* The held-back tail is the whole point: an image whose last bytes happen to
 * look like the start of the terminator must not have them written, and an
 * image that merely contains the boundary's opening dashes must not be cut
 * short there. */
static void test_the_terminator_is_not_written(void)
{
    struct multipart_s mp;
    struct sink_s      sink;
    /* Ends with a CR LF, which is the first two bytes of the terminator. */
    const char payload[] = "image data\r\n";
    size_t     len = sizeof(payload) - 1;

    run(&mp, &sink, "--bound", payload, len, true);

    TEST_ASSERT_TRUE(multipart_complete(&mp));
    TEST_ASSERT_EQUAL_size_t(len, sink.len);
    TEST_ASSERT_EQUAL_MEMORY(payload, sink.data, len);
}

/* A payload that contains something that looks like the boundary but is not
 * preceded by CRLF runs on. */
static void test_a_boundary_like_run_inside_the_payload(void)
{
    struct multipart_s mp;
    struct sink_s      sink;
    const char         payload[] = "before--bound-after";
    size_t             len = sizeof(payload) - 1;

    run(&mp, &sink, "--bound", payload, len, true);

    TEST_ASSERT_TRUE(multipart_complete(&mp));
    TEST_ASSERT_EQUAL_size_t(len, sink.len);
    TEST_ASSERT_EQUAL_MEMORY(payload, sink.data, len);
}

/* A body that stops in the middle is not complete, and the caller must be
 * able to tell -- that is what decides between esp_ota_end() and
 * esp_ota_abort(). */
static void test_a_truncated_body_is_not_complete(void)
{
    struct multipart_s mp;
    struct sink_s      sink;

    run(&mp, &sink, "--bound", "half an image", 13, false);

    TEST_ASSERT_FALSE(multipart_complete(&mp));
    TEST_ASSERT_FALSE(multipart_failed(&mp));
    TEST_ASSERT_EQUAL_INT(1, sink.begins);
}

/* A body that stops inside the headers never opens the partition at all. */
static void test_a_body_that_stops_in_the_headers(void)
{
    struct multipart_s mp;
    struct sink_s      sink;

    memset(&sink, 0, sizeof(sink));
    multipart_init(&mp, sink_begin, sink_write, &sink);

    multipart_feed_block(&mp, "--bound\r\nContent-Type: application/oct", 38);

    TEST_ASSERT_FALSE(multipart_complete(&mp));
    TEST_ASSERT_EQUAL_INT(0, sink.begins);
    TEST_ASSERT_EQUAL_size_t(0, sink.len);
}

/* A sink that refuses stops the scan rather than being called again -- there
 * is no point streaming a megabyte into a partition that would not open. */
static void test_a_refusing_sink_stops_the_scan(void)
{
    struct multipart_s mp;
    struct sink_s      sink;

    memset(&sink, 0, sizeof(sink));
    sink.refuse_begin = true;
    multipart_init(&mp, sink_begin, sink_write, &sink);
    multipart_feed_block(&mp, "--b\r\n\r\nlots of payload\r\n--b--\r\n", 31);

    TEST_ASSERT_TRUE(multipart_failed(&mp));
    TEST_ASSERT_EQUAL_size_t(0, sink.len);

    memset(&sink, 0, sizeof(sink));
    sink.refuse_write = true;
    multipart_init(&mp, sink_begin, sink_write, &sink);
    multipart_feed_block(&mp, "--b\r\n\r\nlots of payload\r\n--b--\r\n", 31);

    TEST_ASSERT_TRUE(multipart_failed(&mp));
    TEST_ASSERT_EQUAL_INT(1, sink.begins);
}

/* Headers longer than the line buffer are truncated rather than refused:
 * nothing in them is read, and a long filename is an ordinary thing to
 * send. */
static void test_an_over_long_header_line_is_tolerated(void)
{
    struct multipart_s mp;
    struct sink_s      sink;
    char               body[1024];
    char               filename[300];
    size_t             n;

    memset(filename, 'f', sizeof(filename) - 1);
    filename[sizeof(filename) - 1] = '\0';

    n = (size_t)snprintf(body, sizeof(body),
                         "--b\r\nContent-Disposition: form-data; filename=\"%s\"\r\n"
                         "\r\npayload\r\n--b--\r\n",
                         filename);

    memset(&sink, 0, sizeof(sink));
    multipart_init(&mp, sink_begin, sink_write, &sink);
    multipart_feed_block(&mp, body, n);

    TEST_ASSERT_TRUE(multipart_complete(&mp));
    TEST_ASSERT_EQUAL_size_t(7, sink.len);
    TEST_ASSERT_EQUAL_MEMORY("payload", sink.data, 7);
}

/* An empty image reaches the end without anything having been written, which
 * is what lets the caller refuse it: esp_ota_end() on a partition nothing was
 * written to would set a boot flag on nothing. */
static void test_an_empty_payload_writes_nothing(void)
{
    struct multipart_s mp;
    struct sink_s      sink;

    run(&mp, &sink, "--bound", "", 0, true);

    TEST_ASSERT_TRUE(multipart_complete(&mp));
    TEST_ASSERT_EQUAL_INT(1, sink.begins);
    TEST_ASSERT_EQUAL_size_t(0, sink.len);
}

/* Anything after the terminator is ignored: curl sends a trailing CRLF, and
 * a browser may send more parts this scanner has no use for. */
static void test_trailing_bytes_are_ignored(void)
{
    struct multipart_s mp;
    struct sink_s      sink;

    run(&mp, &sink, "--bound", "image", 5, true);
    TEST_ASSERT_TRUE(multipart_complete(&mp));

    multipart_feed_block(&mp, "and some more\r\n--bound--\r\n", 26);

    TEST_ASSERT_TRUE(multipart_complete(&mp));
    TEST_ASSERT_EQUAL_size_t(5, sink.len);
}

void test_multipart_run(void)
{
    RUN_TEST(test_a_normal_upload);
    RUN_TEST(test_chunking_does_not_matter);
    RUN_TEST(test_an_over_long_boundary_is_refused);
    RUN_TEST(test_the_longest_allowed_boundary_works);
    RUN_TEST(test_an_empty_boundary_is_refused);
    RUN_TEST(test_the_terminator_is_not_written);
    RUN_TEST(test_a_boundary_like_run_inside_the_payload);
    RUN_TEST(test_a_truncated_body_is_not_complete);
    RUN_TEST(test_a_body_that_stops_in_the_headers);
    RUN_TEST(test_a_refusing_sink_stops_the_scan);
    RUN_TEST(test_an_over_long_header_line_is_tolerated);
    RUN_TEST(test_an_empty_payload_writes_nothing);
    RUN_TEST(test_trailing_bytes_are_ignored);
}
