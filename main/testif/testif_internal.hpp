/**
 * @file testif_internal.hpp
 *
 * What the parts of the test interface expose to each other, and to nothing
 * else. Simulator-and-bench only by construction: nobody includes this from a
 * file that is not already inside `#if CONFIG_IDF_TARGET_LINUX ||
 * CONFIG_OHEZ_TESTIF`.
 *
 * The shape is one table in testif.cpp and one handler per command, so that
 * adding a command is adding a function and a row -- the same arrangement
 * webui.cpp uses for its routes, and for the same reason.
 */
#ifndef TESTIF_INTERNAL_HPP
#define TESTIF_INTERNAL_HPP

#include <stddef.h>
#include <stdint.h>

#include "testif_parse.h"

/**
 * One command.
 *
 * @param cmd   the tokenised request; argv[0] is the command word itself.
 * @param out   where a reply payload goes, already empty. Left empty for the
 *   many commands whose whole answer is "ok".
 * @return NULL when it worked, or a short reason that becomes `err <reason>`.
 *   A reason is a fixed string, never formatted, so that a caller can match on
 *   it and a test can assert on it.
 */
typedef const char *(*testif_fn_t)(const testif_cmd_t *cmd, char *out, size_t out_size);

/* ------------------------------------------------------------------- touch */

/** Register the synthetic pointer, beside the SDL mouse. */
void testif_touch_init(void);

const char *testif_cmd_tap(const testif_cmd_t *cmd, char *out, size_t out_size);
const char *testif_cmd_longpress(const testif_cmd_t *cmd, char *out, size_t out_size);
const char *testif_cmd_swipe(const testif_cmd_t *cmd, char *out, size_t out_size);
const char *testif_cmd_press(const testif_cmd_t *cmd, char *out, size_t out_size);
const char *testif_cmd_move(const testif_cmd_t *cmd, char *out, size_t out_size);
const char *testif_cmd_release(const testif_cmd_t *cmd, char *out, size_t out_size);

/* ------------------------------------------------------------------ report */

const char *testif_cmd_screen(const testif_cmd_t *cmd, char *out, size_t out_size);
const char *testif_cmd_status(const testif_cmd_t *cmd, char *out, size_t out_size);
const char *testif_cmd_heap(const testif_cmd_t *cmd, char *out, size_t out_size);
const char *testif_cmd_config(const testif_cmd_t *cmd, char *out, size_t out_size);
const char *testif_cmd_set(const testif_cmd_t *cmd, char *out, size_t out_size);

/* --------------------------------------------------------------- navigation */

const char *testif_cmd_nav(const testif_cmd_t *cmd, char *out, size_t out_size);
const char *testif_cmd_settings(const testif_cmd_t *cmd, char *out, size_t out_size);
const char *testif_cmd_calibrate(const testif_cmd_t *cmd, char *out, size_t out_size);

/* ------------------------------------------------------------------- shot */

/** Register the /screenshot.raw route, once the web server is up. */
void testif_shot_init(void);

const char *testif_cmd_shot(const testif_cmd_t *cmd, char *out, size_t out_size);

/* ------------------------------------------------------------------ shared */

/**
 * Read a coordinate pair, checked against the panel.
 *
 * Out of range is refused rather than clamped: a clamped tap lands on
 * something real and quietly tests the wrong widget, which is worse than a
 * command that says no.
 */
const char *testif_coords(const testif_cmd_t *cmd, unsigned first, int32_t *x, int32_t *y);

/** A decimal argument, or `def` when the request did not carry one. */
bool testif_arg_int(const testif_cmd_t *cmd, unsigned index, long *out);

#endif /* TESTIF_INTERNAL_HPP */
