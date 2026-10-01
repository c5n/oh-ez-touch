/**
 * @file ui_pin.cpp
 *
 * See ui_pin.hpp.
 */
#include "ui_pin.hpp"

#include <atomic>
#include <stdio.h>
#include <string.h>

#include "config/pin_code.h"
#include "control/backlight_control.hpp"
#include "port/port_sys.h"
#include "ui_beep.hpp"
#include "ui_motion.hpp"
#include "ui_settings.hpp"
#include "ui_style.hpp"
#include "ui_widgets.hpp"

extern BacklightControl tft_backlight;

/* An unlock is forgotten after this long without a touch, if the backlight
 * has not dimmed before then. */
#define PIN_IDLE_MS 60000u

#define PAD_BAR_H 40
#define PAD_GAP   4
#define DOT_SIZE  12

enum pad_mode_e
{
    PAD_GUARD,      /* checking a PIN, then running `then` */
    PAD_NEW_FIRST,  /* a new PIN, first time */
    PAD_NEW_REPEAT, /* the same again */
};

static bool                 unlocked[PIN_SCOPE_COUNT];
static std::atomic<uint8_t> clear_requests; /* one bit per scope */
static struct pin_lockout_s lockout[PIN_SCOPE_COUNT];

static struct
{
    lv_obj_t         *root;
    lv_obj_t         *message;
    lv_obj_t         *dots[PIN_CODE_MAX_LEN];
    lv_obj_t         *dot_row;
    lv_obj_t         *bar;
    lv_obj_t         *keys[12];
    enum pad_mode_e   mode;
    enum pin_scope_e  scope;
    char              digits[PIN_CODE_MAX_LEN + 1];
    char              first[PIN_CODE_MAX_LEN + 1];
    ui_pin_then_cb    then;
    void             *then_arg;
    ui_pin_changed_cb changed;
    void             *changed_arg;
    uint32_t          shown_remaining_s; /* what the lockout text last said */
} pad;

static const char *scope_title(enum pin_scope_e scope)
{
    return (scope == PIN_SCOPE_ITEM) ? "Item PIN" : "System PIN";
}

/* ------------------------------------------------------------ the display */

static void dots_show(void)
{
    size_t n = strlen(pad.digits);

    for (size_t i = 0; i < PIN_CODE_MAX_LEN; i++)
    {
        lv_obj_set_hidden(pad.dots[i], i >= n);
    }
}

static void prompt_default(void)
{
    const char *text = "Enter PIN";

    if (pad.mode == PAD_NEW_FIRST)
        text = "New PIN\n4 to 8 digits";
    else if (pad.mode == PAD_NEW_REPEAT)
        text = "Repeat the new PIN";

    lv_label_set_text(pad.message, text);
}

/* Whether the scope is refusing entries, and the pad saying so. Asked on
 * every key and once a second from ui_pin_loop() while the pad is up. */
static bool lockout_show(void)
{
    uint32_t remaining_ms;

    if (pad.mode != PAD_GUARD
        || pin_lockout_active(&lockout[pad.scope], port_tick_ms(), &remaining_ms) == false)
    {
        if (pad.shown_remaining_s != 0)
        {
            pad.shown_remaining_s = 0;
            prompt_default();
        }
        return false;
    }

    uint32_t s = (remaining_ms + 999) / 1000;

    if (s != pad.shown_remaining_s)
    {
        pad.shown_remaining_s = s;
        lv_label_set_text_fmt(pad.message, "Too many tries\nwait %lu s", (unsigned long)s);
    }

    return true;
}

static void shake_exec(void *obj, int32_t v)
{
    lv_obj_set_style_translate_x((lv_obj_t *)obj, v, 0);
}

/* The message shakes its head. Only the label moves, and only by a few
 * pixels, so it is a few small invalidations rather than a repaint. */
static void shake(void)
{
    if (ui_motion_enabled() == false)
        return;

    lv_anim_t a;

    lv_anim_init(&a);
    lv_anim_set_var(&a, pad.message);
    lv_anim_set_exec_cb(&a, shake_exec);
    lv_anim_set_values(&a, -6, 6);
    lv_anim_set_duration(&a, 45);
    lv_anim_set_reverse_duration(&a, 45);
    lv_anim_set_repeat_count(&a, 3);
    lv_anim_set_completed_cb(&a, [](lv_anim_t *done) {
        lv_obj_set_style_translate_x((lv_obj_t *)done->var, 0, 0);
    });
    lv_anim_start(&a);
}

static void say(const char *text)
{
    lv_label_set_text(pad.message, text);
    shake();
}

/* ------------------------------------------------------------ open, close */

static void pad_close(void)
{
    if (pad.root != NULL)
        lv_obj_delete(pad.root);

    pad.root = NULL;
    pad.message = NULL;
    pad.dot_row = NULL;
    pad.bar = NULL;
    memset(pad.dots, 0, sizeof(pad.dots));
    memset(pad.keys, 0, sizeof(pad.keys));
    memset(pad.digits, 0, sizeof(pad.digits));
    memset(pad.first, 0, sizeof(pad.first));
    pad.then = NULL;
    pad.changed = NULL;
}

static void cancel_event(lv_event_t *e)
{
    LV_UNUSED(e);

    BEEPER_EVENT_CANCEL();
    pad_close();
}

static void submit(void)
{
    if (pad.mode == PAD_GUARD)
    {
        enum pin_scope_e scope = pad.scope;

        if (pin_store_check(scope, pad.digits) == false)
        {
            pin_lockout_fail(&lockout[scope], port_tick_ms());
            pad.digits[0] = '\0';
            dots_show();
            BEEPER_EVENT_ERROR();

            if (lockout_show() == false)
                say("Wrong PIN");
            else
                shake();
            return;
        }

        ui_pin_then_cb then = pad.then;
        void          *arg = pad.then_arg;

        pin_lockout_success(&lockout[scope]);
        unlocked[scope] = true;
        BEEPER_EVENT_ACCEPT();

        /* Closed first: what `then` opens may be a screen, and it must not
         * come up underneath a pad that is about to go. */
        pad_close();

        if (then != NULL)
            then(arg);
        return;
    }

    if (pin_code_valid(pad.digits) == false)
    {
        BEEPER_EVENT_ERROR();
        say("4 to 8 digits");
        return;
    }

    if (pad.mode == PAD_NEW_FIRST)
    {
        strlcpy(pad.first, pad.digits, sizeof(pad.first));
        pad.digits[0] = '\0';
        pad.mode = PAD_NEW_REPEAT;
        dots_show();
        prompt_default();
        BEEPER_EVENT_TICK();
        return;
    }

    /* PAD_NEW_REPEAT */
    if (strcmp(pad.first, pad.digits) != 0)
    {
        pad.mode = PAD_NEW_FIRST;
        pad.digits[0] = '\0';
        pad.first[0] = '\0';
        dots_show();
        BEEPER_EVENT_ERROR();
        say("PINs differ\nstart again");
        return;
    }

    enum pin_set_result_e result = pin_store_set(pad.scope, pad.digits);

    if (result == PIN_SET_SAME_AS_OTHER)
    {
        pad.mode = PAD_NEW_FIRST;
        pad.digits[0] = '\0';
        pad.first[0] = '\0';
        dots_show();
        BEEPER_EVENT_ERROR();
        say((pad.scope == PIN_SCOPE_ITEM) ? "Must differ from\nthe System PIN"
                                          : "Must differ from\nthe Item PIN");
        return;
    }

    ui_pin_changed_cb changed = pad.changed;
    void             *arg = pad.changed_arg;

    if (result == PIN_SET_OK)
    {
        unlocked[pad.scope] = true;
        pin_lockout_success(&lockout[pad.scope]);
    }

    pad_close();

    if (changed != NULL)
        changed(result, arg);
}

static void key_event(lv_event_t *e)
{
    char   key = (char)(uintptr_t)lv_event_get_user_data(e);
    size_t n = strlen(pad.digits);

    if (lockout_show() == true)
    {
        BEEPER_EVENT_ERROR();
        shake();
        return;
    }

    if (key == 'b')
    {
        if (n > 0)
            pad.digits[n - 1] = '\0';
    }
    else if (key == 'k')
    {
        submit();
        return;
    }
    else if (n < PIN_CODE_MAX_LEN)
    {
        pad.digits[n] = key;
        pad.digits[n + 1] = '\0';

        /* The prompt gives way to the dots once there is something to show,
         * and a "Wrong PIN" from the try before goes with it. */
        if (n == 0)
            lv_label_set_text(pad.message, "");
    }

    dots_show();
}

/* 1 2 3 / 4 5 6 / 7 8 9 / ⌫ 0 ✓ -- the telephone layout, which is the one
 * every alarm panel uses. 'b' and 'k' stand for the two that are not digits. */
static const char keys[12] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', 'b', '0', 'k'};

static void keypad_create(lv_obj_t *parent)
{
    lv_obj_update_layout(parent);

    int32_t w = lv_obj_get_content_width(parent);
    int32_t h = lv_obj_get_content_height(parent);
    int32_t bw = (w - 2 * PAD_GAP) / 3;
    int32_t bh = (h - 3 * PAD_GAP) / 4;

    for (int i = 0; i < 12; i++)
    {
        char text[2] = {keys[i], '\0'};
        const char *label = text;

        if (keys[i] == 'b')
            label = LV_SYMBOL_BACKSPACE;
        else if (keys[i] == 'k')
            label = LV_SYMBOL_OK;

        lv_obj_t *btn = ui_themed_button(parent, label);

        lv_obj_set_size(btn, bw, bh);
        lv_obj_set_pos(btn, (i % 3) * (bw + PAD_GAP), (i / 3) * (bh + PAD_GAP));
        lv_obj_add_event_cb(btn, key_event, LV_EVENT_CLICKED, (void *)(uintptr_t)keys[i]);
        pad.keys[i] = btn;
    }
}

static void pad_open(enum pin_scope_e scope, enum pad_mode_e mode)
{
    bool    portrait = ui_style_portrait();
    int32_t hres = lv_display_get_horizontal_resolution(NULL);
    int32_t vres = lv_display_get_vertical_resolution(NULL);
    char    title[40];

    pad_close();

    pad.scope = scope;
    pad.mode = mode;
    pad.shown_remaining_s = 0;

    /* Full-screen, opaque and clickable, like the settings overlays: nothing
     * underneath may show through or take a touch aimed at the pad. */
    pad.root = lv_obj_create(lv_layer_top());
    lv_obj_set_scrollable(pad.root, false);
    lv_obj_set_pos(pad.root, 0, 0);
    lv_obj_set_size(pad.root, hres, vres);
    lv_obj_add_style(pad.root, &ui_style_window, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pad.root, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pad.root, 0, 0);
    lv_obj_set_style_pad_all(pad.root, 0, 0);
    lv_obj_set_clickable(pad.root, true);

    snprintf(title, sizeof(title), "%s%s", (mode == PAD_GUARD) ? "" : "Set ", scope_title(scope));
    pad.bar = ui_back_bar(pad.root, LV_SYMBOL_CLOSE, title, PAD_BAR_H, cancel_event);

    /* What is said and what has been typed on one side, the keys on the
     * other -- or above and below, upright. Side by side is what gives a
     * 240 px tall panel keys of a fingertip's height. */
    lv_obj_t *info = ui_plain_container(pad.root);
    lv_obj_t *keypad = ui_plain_container(pad.root);
    int32_t   body_h = vres - PAD_BAR_H;

    if (portrait == true)
    {
        lv_obj_set_pos(info, 0, PAD_BAR_H);
        lv_obj_set_size(info, hres, body_h / 4);
        lv_obj_set_pos(keypad, 0, PAD_BAR_H + body_h / 4);
        lv_obj_set_size(keypad, hres, body_h - body_h / 4);
    }
    else
    {
        lv_obj_set_pos(info, 0, PAD_BAR_H);
        lv_obj_set_size(info, hres * 3 / 8, body_h);
        lv_obj_set_pos(keypad, hres * 3 / 8, PAD_BAR_H);
        lv_obj_set_size(keypad, hres - hres * 3 / 8, body_h);
    }

    lv_obj_set_style_pad_all(keypad, PAD_GAP, 0);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(info, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(info, 10, 0);

    pad.message = lv_label_create(info);
    lv_obj_add_style(pad.message, &ui_style_label, LV_PART_MAIN);
    lv_obj_set_style_text_align(pad.message, LV_TEXT_ALIGN_CENTER, 0);

    /* Dots rather than asterisks: the families' fonts are subset, and a
     * filled circle is something every one of them can draw as an object. The
     * slider's indicator is the theme's own "filled" colour. */
    pad.dot_row = ui_plain_container(info);
    lv_obj_set_size(pad.dot_row, LV_SIZE_CONTENT, DOT_SIZE);
    lv_obj_set_flex_flow(pad.dot_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(pad.dot_row, 6, 0);

    for (int i = 0; i < PIN_CODE_MAX_LEN; i++)
    {
        lv_obj_t *dot = ui_plain_container(pad.dot_row);

        lv_obj_set_size(dot, DOT_SIZE, DOT_SIZE);
        lv_obj_add_style(dot, &ui_style_slider_indicator, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_hidden(dot, true);
        pad.dots[i] = dot;
    }

    keypad_create(keypad);
    prompt_default();
    lockout_show();
    BEEPER_EVENT_SCREEN();
}

/* ------------------------------------------------------------- the API */

void ui_pin_guard(enum pin_scope_e scope, ui_pin_then_cb then, void *arg)
{
    if ((unsigned)scope >= PIN_SCOPE_COUNT)
        return;

    if (pin_store_is_set(scope) == false || unlocked[scope] == true)
    {
        if (then != NULL)
            then(arg);
        return;
    }

    pad_open(scope, PAD_GUARD);
    pad.then = then;
    pad.then_arg = arg;
}

void ui_pin_change(enum pin_scope_e scope, ui_pin_changed_cb done, void *arg)
{
    if ((unsigned)scope >= PIN_SCOPE_COUNT)
        return;

    pad_open(scope, PAD_NEW_FIRST);
    pad.changed = done;
    pad.changed_arg = arg;
}

bool ui_pin_is_unlocked(enum pin_scope_e scope)
{
    return ((unsigned)scope < PIN_SCOPE_COUNT) && unlocked[scope];
}

void ui_pin_lock_all(void)
{
    bool system_was = unlocked[PIN_SCOPE_SYSTEM];

    memset(unlocked, 0, sizeof(unlocked));
    pad_close();

    /* A System page left open would outlive its unlock. Only when there was
     * one to lose: a panel with no System PIN never unlocks, and must not be
     * thrown off its settings page for having none. */
    if (system_was == true)
        ui_settings_leave_protected();
}

bool ui_pin_is_open(void)
{
    return pad.root != NULL;
}

enum pin_scope_e ui_pin_open_scope(void)
{
    return pad.scope;
}

bool ui_pin_key_area(char key, lv_area_t *out)
{
    lv_obj_t *obj = NULL;

    if (pad.root == NULL)
        return false;

    if (key == 'x')
        obj = pad.bar;

    for (int i = 0; obj == NULL && i < 12; i++)
        if (keys[i] == key)
            obj = pad.keys[i];

    if (obj == NULL)
        return false;

    lv_obj_update_layout(obj);
    lv_obj_get_coords(obj, out);
    return true;
}

void ui_pin_request_clear(enum pin_scope_e scope)
{
    if ((unsigned)scope < PIN_SCOPE_COUNT)
        clear_requests.fetch_or((uint8_t)(1u << scope));
}

void ui_pin_loop(void)
{
    static uint32_t last_tick_ms;
    bool            any = false;
    uint8_t         clears = clear_requests.exchange(0);

    for (int i = 0; i < PIN_SCOPE_COUNT; i++)
    {
        if ((clears & (1u << i)) == 0)
            continue;

        pin_store_clear((enum pin_scope_e)i);
        unlocked[i] = false;

        /* A pad asking for a PIN that no longer exists would ask forever. */
        if (pad.root != NULL && pad.scope == (enum pin_scope_e)i && pad.mode == PAD_GUARD)
            pad_close();
    }

    for (int i = 0; i < PIN_SCOPE_COUNT; i++)
        any = any || unlocked[i];

    if (any == true || pad.root != NULL)
    {
        uint32_t inactive = lv_display_get_inactive_time(NULL);

        if (tft_backlight.isDimmed() == true
            || (inactive != UINT32_MAX && inactive >= PIN_IDLE_MS))
        {
            ui_pin_lock_all();
            return;
        }
    }

    /* The countdown, at most once every quarter second. */
    if (pad.root != NULL && (uint32_t)(port_tick_ms() - last_tick_ms) >= 250)
    {
        last_tick_ms = port_tick_ms();
        lockout_show();
    }
}
