/**
 * @file ui_messagebox.cpp
 *
 * The message box that floats over the page, and the frame indicator that is
 * the way back to it once it has been folded away.
 */
#include "ui_messagebox.hpp"

#include "debug.h"
#include "frames/ui_frame.hpp"
#include "port/port_sys.h"
#include "ui_beep.hpp"
#include "ui_motion.hpp"
#include "ui_style.hpp"

#include <stdio.h>

/* What the box puts between its border and its text. It used to be part of
 * ui_style_info and is here instead because the box itself can no longer carry
 * it: the title bar has to reach the border, so only the content is padded. */
#define PAD_CONTENT (LV_DPI_DEF / 10)

bool Messagebox::covered = false;

/* FNV-1a over the topic and the text, seeded with the severity. See `said`. */
static uint32_t hash(const char *text, uint32_t seed)
{
    uint32_t h = seed;

    for (; text != NULL && *text != '\0'; text++)
    {
        h ^= (uint32_t)(unsigned char)*text;
        h *= 16777619u;
    }

    return h;
}

/* Black or white, whichever the title bar's fill can carry.
 *
 * The severity colours are picked in the theme table to read as a warning or
 * an error against the panel, not to be written on, and across six variants
 * they run from a pale LCARS yellow to a near-black jarvis brown -- so one
 * fixed ink would be invisible at one end or the other. The bar is the only
 * surface in this UI whose colour is not known when the theme is written,
 * which is why this is the only place that has to work its ink out. */
static lv_color_t bar_ink(uint32_t bg)
{
    return (lv_color_luminance(lv_color_hex(bg)) > 128) ? lv_color_black() : lv_color_white();
}

static enum ui_notice_e notice_of(enum Messagebox::messagebox_type_e kind)
{
    switch (kind)
    {
    case Messagebox::WARNING: return UI_NOTICE_WARNING;
    case Messagebox::ERROR:   return UI_NOTICE_ERROR;
    default:                  return UI_NOTICE_INFO;
    }
}

/* --------------------------------------------------------------- the box */

void Messagebox::build(void)
{
    /* The panel used to build a private style here. It now shares the theme's,
     * which is what lets a theme change repaint a box that is already on
     * screen -- lv_obj_report_style_change() reaches the top layer, but it can
     * only refresh a style someone else owns. */
    mb = lv_msgbox_create(lv_layer_top());
    lv_obj_set_scrollable(mb, false);
    lv_obj_add_style(mb, &ui_style_info, LV_PART_MAIN);
    lv_obj_set_width(mb, lv_display_get_horizontal_resolution(NULL) * 9 / 10);
    lv_obj_set_height(mb, LV_SIZE_CONTENT);

    /* A ceiling on what the content may grow to. The old plain-object banner
     * had none and got away with it, but the title bar now takes vertical room
     * the text used to have, and one caller's text is a sitemap URL of no
     * particular length. Past this the content area scrolls, which is what the
     * msgbox's content is for. */
    lv_obj_set_style_max_height(mb, lv_pct(90), 0);

    /* The title bar reaches the box's edges, so the box keeps no padding of
     * its own and the content below takes it on instead. Clipping the corners
     * is what stops a square-ended bar showing outside the rounded top of the
     * box on the variants that round this one. */
    lv_obj_set_style_pad_all(mb, 0, 0);
    lv_obj_set_style_clip_corner(mb, true, 0);

    /* ---- the title bar ----
     *
     * The msgbox's header, wearing the same surface as an item window's header
     * and the settings footer -- which is the whole point: the box stops being
     * a slab of severity colour with words on it and becomes a window of the
     * same family as every other one in this UI, with its own title bar and a
     * button at the end of it.
     *
     * lv_msgbox_add_title() is what creates the header, so there is nothing to
     * style before this line. */
    title = lv_msgbox_add_title(mb, "");

    lv_obj_t *bar = lv_msgbox_get_header(mb);

    lv_obj_add_style(bar, &ui_style_win_header, LV_PART_MAIN);
    lv_obj_set_height(bar, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(bar, 3, 0);
    /* Square, against the style's own: LCARS makes its headers stadiums, and a
     * stadium spanning the full width of a box would leave the fill showing
     * through at all four corners. */
    lv_obj_set_style_radius(bar, 0, 0);
    /* Nothing in ui_style_win_header sets one, and the bar is a flex row: with
     * no column gap the title runs into the button. */
    lv_obj_set_style_pad_column(bar, 8, 0);

    /* One line. lv_msgbox_add_title() gives it flex_grow, so it is the button
     * that decides where the dots fall -- and `topic` is what getTopic()
     * reports, because LV_LABEL_LONG_DOT writes them into this label's text. */
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);

    fold_btn = lv_msgbox_add_header_button(mb, NULL);

    /* Styled as every other button in this UI is, press feedback included,
     * rather than left at the header button class's bare defaults. The glyph
     * is a label rather than the icon lv_msgbox_add_header_button() takes,
     * which would make an lv_image of it -- a themed button with a label in it
     * is what ui_themed_button() builds, and this is one of those. */
    lv_obj_add_style(fold_btn, &ui_style_btn, LV_PART_MAIN);
    lv_obj_add_style(fold_btn, &ui_style_btn_checked,
                     ui_style_selector(LV_PART_MAIN, LV_STATE_PRESSED));
    ui_motion_pressable(fold_btn);

    lv_obj_t *glyph = lv_label_create(fold_btn);

    lv_label_set_text(glyph, LV_SYMBOL_MINUS);
    lv_obj_center(glyph);

    /* Minimise rather than close, and it says so: LV_SYMBOL_CLOSE is what the
     * indicator wears for an error, and the two must not be the same glyph.
     * The extended area is because a glyph-sized button is a glyph-sized
     * target -- it buys 8 px of touch on every side of it. */
    lv_obj_set_ext_click_area(fold_btn, 8);
    lv_obj_add_event_cb(fold_btn, fold_event, LV_EVENT_CLICKED, this);

    /* ---- what it says ---- */
    body = lv_msgbox_add_text(mb, "");
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);

    /* Where the padding the box gave up reappears. The content area arrives
     * with none -- lv_theme_simple matches on the exact class, and the
     * msgbox's header, content and footer are three classes of their own, so
     * all three come up transparent, borderless and unpadded. That is why the
     * bar above had to be given a surface by hand. */
    lv_obj_set_style_pad_all(lv_msgbox_get_content(mb), PAD_CONTENT, 0);

    /* ---- the restart button ----
     *
     * Last, because the footer is a child like any other and the box is a flex
     * column: the header is moved to index 0 by the widget, the content was
     * made by the constructor, and this lands under both.
     *
     * Content-sized, against the class defaults -- the footer is a fixed
     * LV_DPI_DEF / 3 tall and the button 100 % of that, which on a 240 px
     * panel is a bar deeper than most of the messages it sits under. Content
     * both ways instead, with the bottom padding the content area already
     * carries so the button is not flush against the border. */
    restart_btn = lv_msgbox_add_footer_button(mb, "Restart");
    footer = lv_msgbox_get_footer(mb);

    lv_obj_set_height(footer, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_bottom(footer, PAD_CONTENT, 0);
    lv_obj_set_height(restart_btn, LV_SIZE_CONTENT);

    /* The same styling as the fold button above, and for the same reason: a
     * footer button arrives at its class's bare defaults, and every other
     * button in this UI acknowledges a press. */
    lv_obj_add_style(restart_btn, &ui_style_btn, LV_PART_MAIN);
    lv_obj_add_style(restart_btn, &ui_style_btn_checked,
                     ui_style_selector(LV_PART_MAIN, LV_STATE_PRESSED));
    ui_motion_pressable(restart_btn);

    /* Flat, whatever the family does with its buttons. restyle() replaces the
     * fill with the title bar's and JARVIS gives its buttons a vertical
     * gradient, so with the gradient left in place the button ran from the
     * bar's colour into the theme's dark end and came out neither. A property
     * of this button rather than of the theme, so it is set once. */
    lv_obj_set_style_bg_grad_dir(restart_btn, LV_GRAD_DIR_NONE, 0);

    /* No user data, unlike the fold button: there is one thing this can do and
     * it is the same for both boxes. */
    lv_obj_add_event_cb(restart_btn, restart_event, LV_EVENT_CLICKED, NULL);

    /* Nothing offers it yet. create() says so again on every message. */
    lv_obj_set_hidden(footer, true);
}

/* Everything the box wears that comes from the theme table rather than from
 * the message: the severity fill and its ink, the two faces, and the sizes of
 * the two buttons.
 *
 * Called by create(), and again by restyle_all() when the theme changes under
 * a box that is already up. ui_style_apply() refills the shared styles in
 * place and reports them, which repaints everything a box wears by reference
 * -- but these are local properties worked out from the table, and a style
 * refresh cannot redo those. Before this was its own function they were simply
 * left standing: a box raised under JARVIS and still up when the panel went
 * back to Material kept JARVIS's black title ink, on Material's dark red
 * bar. */
void Messagebox::restyle(void)
{
    if (mb == NULL)
        return;

    const struct ui_theme_s *t = ui_style_theme();

    /* The severity is the colour of the title bar, and of nothing else.
     *
     * It used to be the fill of the whole box, which is why the old one was a
     * red slab: the text colour a variant picks is picked against its info
     * background, so on the warning and error fills -- which the table chooses
     * to read as severity against the *panel* -- the words were left to fend
     * for themselves. A bar is a small enough surface to give an ink of its
     * own, and it is also what the frame's indicator wears, so the two say the
     * same thing in the same colour. */
    lv_obj_t *bar = lv_msgbox_get_header(mb);

    lv_obj_remove_style(bar, &ui_style_info_warning, LV_PART_MAIN);
    lv_obj_remove_style(bar, &ui_style_info_error, LV_PART_MAIN);
    lv_obj_remove_style(restart_btn, &ui_style_info_warning, LV_PART_MAIN);
    lv_obj_remove_style(restart_btn, &ui_style_info_error, LV_PART_MAIN);

    if (kind == INFO)
    {
        /* Back to the header surface entire -- fill and ink both. Removing the
         * local property rather than setting a colour, because what belongs
         * there is whatever the variant chose for its own headers. The button
         * goes back to the theme's own, which is the one case it is left on:
         * no message that offers a restart is an info, and if one ever is it
         * will want the same treatment the two below get. */
        lv_obj_remove_local_style_prop(bar, LV_STYLE_TEXT_COLOR, 0);
        lv_obj_remove_local_style_prop(restart_btn, LV_STYLE_TEXT_COLOR, 0);
    }
    else
    {
        lv_style_t *severity = (kind == WARNING) ? &ui_style_info_warning
                                                 : &ui_style_info_error;
        uint32_t    fill = (kind == WARNING) ? t->info_warning_bg : t->info_error_bg;

        lv_obj_add_style(bar, severity, LV_PART_MAIN);
        lv_obj_set_style_text_color(bar, bar_ink(fill), 0);

        /* The Restart button takes the bar's fill rather than the theme's own
         * button surface, and has to: ui_style_info and ui_style_btn are the
         * same colour in four of the six variants -- a panel and a button are
         * the same kind of surface in Material and in LCARS -- so a button
         * painted the usual way and then put on this box sinks into it.
         * Material and JARVIS save it with the hairline border they give
         * their buttons; LCARS gives its none, and there the word sat on the
         * panel with nothing around it at all.
         *
         * The bar has already solved exactly this, in every variant, or its
         * title would not be readable either. Only the fill and the ink come
         * from it: the radius, the padding and the press feedback stay the
         * theme's, so it is still that family's button. The same shared style
         * the bar wears, so a theme change repaints both without help. */
        lv_obj_add_style(restart_btn, severity, LV_PART_MAIN);
        lv_obj_set_style_text_color(restart_btn, bar_ink(fill), 0);
    }

    /* Square, and sized from the face rather than left at the header button
     * class's LV_DPI_DEF / 3 by 100%: the three families' faces differ by
     * several pixels at the same nominal size, and the bar is content-sized
     * around this. */
    int32_t btn = lv_font_get_line_height(t->font_small) + 8;

    lv_obj_set_size(fold_btn, btn, btn);

    /* The caption face, not the state-line face the box inherits from
     * ui_style_info. At 22 px a sitemap URL filled the screen and a two-word
     * topic needed two lines; at 16 px the box is the size of the thing it has
     * to say. */
    lv_obj_set_style_text_font(title, t->font_small, 0);
    lv_obj_set_style_text_font(body, t->font_small, 0);
}

void Messagebox::restyle_all(void)
{
    messagebox.restyle();
    openhab_ui_messagebox.restyle();
}

void Messagebox::create(enum messagebox_type_e type, const char *topic,
                        const char *text, uint16_t timeout)
{
    if (mb == NULL)
    {
#if CONFIG_OHEZ_DEBUG_UI_MESSAGEBOX
        printf("Messagebox::create: Topic: %s   Text: %s\r\n", topic, text);
#endif
        build();
    }

    /* Before restyle(), which reads it. Outside the build() guard above,
     * unlike the widget setup: main.cpp calls create() again on a live box to
     * report the next WLAN state, and a severity applied only on the first
     * call meant an error kept the colour of the info that preceded it. */
    kind = type;
    restyle();

    /* A new message, so no offer until this one makes it. The caller's
     * offerRestart() runs before anything is drawn, so a message that does
     * carry one does not flicker it. */
    restart_offered = false;
    lv_obj_set_hidden(footer, true);

    /* The text goes straight into its label: one caller passes a sitemap URL,
     * which is longer than any buffer worth putting on this stack -- the copy
     * this used to make truncated, deliberately, and -Wformat-truncation was
     * right to say so. LVGL sizes its own. The topic is short by nature and is
     * kept, because the bar dots it; see `topic` in the header. */
    lv_strlcpy(this->topic, topic, sizeof(this->topic));
    lv_label_set_text(title, topic);
    lv_label_set_text(body, text);

    /* Annunciate, unless this is the same box saying the same thing.
     *
     * A box is the panel's only channel for anything that happens without
     * being asked for -- the network coming and going, a sitemap that cannot
     * be reached -- so mapping its severity to a sound covers all of those in
     * one place, and stops any of them needing a beep of its own next to every
     * create() call.
     *
     * It sounds even when a pushed screen is hiding the box, and even when the
     * user has folded it away. That is deliberate: with nothing visible, the
     * sound is the only signal there is. */
    uint32_t now_saying = hash(text, hash(topic, 2166136261u + (uint32_t)type));

    if (now_saying != said)
    {
        /* Something genuinely new to say, so it comes back out. A fold
         * dismisses the message that was folded, not every message after it --
         * and the hash is exactly the test for "is this still that message",
         * which is why the unfold and the sound share it. */
        folded = false;

        if (type == WARNING)
            BEEPER_EVENT_WARNING();
        else if (type == ERROR)
            BEEPER_EVENT_ERROR();
        else
            BEEPER_EVENT_NOTIFY();
    }

    said = now_saying;

    lv_obj_center(mb);

    if (timeout > 0)
        timeout_timestamp = port_millis() + timeout * 1000;
    else
        timeout_timestamp = 0;

    refresh();
    refresh_notice();
}

void Messagebox::offerRestart(void)
{
    if (mb == NULL)
        return;

    /* The box grows by a footer, and re-centres itself: create()'s
     * lv_obj_center() set an alignment rather than a position, so the next
     * layout puts the taller box back in the middle without a call here. */
    restart_offered = true;
    lv_obj_set_hidden(footer, false);
}

void Messagebox::restart_event(lv_event_t *e)
{
    LV_UNUSED(e);

    /* Straight to it, without the settings screen's "Restart the device now?"
     * -- there the button sits on a screen the user opened for other reasons,
     * here the box above it is already the prompt, and a fault report with one
     * button under it does not need a second box to ask whether it meant it.
     *
     * Does not return on either target: the simulator exits, which is the same
     * statement made by a process that cannot reboot itself. */
    port_restart();
}

void Messagebox::destroy(void)
{
    if (mb != NULL)
    {
#if CONFIG_OHEZ_DEBUG_UI_MESSAGEBOX
        printf("Messagebox::destroy: Destroying message box\r\n");
#endif
        /* Not lv_msgbox_close(): that exists to take the backdrop down with
         * the box, and this one has a real parent rather than one the widget
         * made for it. */
        lv_obj_delete(mb);
        mb = NULL;
        title = NULL;
        body = NULL;
        fold_btn = NULL;
        footer = NULL;
        restart_btn = NULL;
        topic[0] = '\0';
        timeout_timestamp = 0;
        said = 0;
        folded = false;
        restart_offered = false;

        refresh_notice();

        /* And no sound, which it has to be: this is reached both when a box
         * times out and when the thing it reported recovered, and the WLAN
         * recovery path calls destroy() and then create("CONNECTED!") -- which
         * would be two sounds for one event. The one recovery with no
         * replacement box is the sitemap, and openhab_ui sounds that one
         * itself. */
    }
}

void Messagebox::loop(void)
{
    if (timeout_timestamp > 0 && port_millis() >= timeout_timestamp)
    {
#if CONFIG_OHEZ_DEBUG_UI_MESSAGEBOX
        printf("Messagebox::loop: messagebox timeout reached\r\n");
#endif
        destroy();
        timeout_timestamp = 0;
    }
}

/* ------------------------------------------------------- folding it away */

void Messagebox::refresh(void) const
{
    if (mb != NULL)
        lv_obj_set_hidden(mb, folded || covered);
}

void Messagebox::fold(void)
{
    folded = true;

    /* The sound a dismissal makes elsewhere in this UI -- the settings
     * screen's "Later" is the same gesture on the same kind of panel. */
    BEEPER_EVENT_CANCEL();
    refresh();
}

void Messagebox::fold_event(lv_event_t *e)
{
    ((Messagebox *)lv_event_get_user_data(e))->fold();
}

void Messagebox::unfold(void)
{
    Messagebox *all[] = {&messagebox, &openhab_ui_messagebox};
    bool        any = false;

    for (Messagebox *b : all)
    {
        if (b->mb == NULL || b->folded == false)
            continue;

        b->folded = false;
        b->refresh();
        any = true;
    }

    /* Only if something actually came back. The indicator stays up while the
     * box is on screen -- it is the severity as much as it is the way back --
     * so touching it is a no-op as often as not. */
    if (any == true)
        BEEPER_EVENT_SCREEN();
}

void Messagebox::cover(bool is_covered)
{
    covered = is_covered;

    messagebox.refresh();
    openhab_ui_messagebox.refresh();
}

void Messagebox::refresh_notice(void)
{
    const Messagebox *all[] = {&messagebox, &openhab_ui_messagebox};
    enum ui_notice_e notice = UI_NOTICE_NONE;

    /* The louder of the two, rather than the first one up. They are
     * independent -- a sitemap error and a WLAN warning can be raised in
     * either order -- and one indicator cannot say both. */
    for (const Messagebox *b : all)
    {
        if (b->isUp() == false)
            continue;

        enum ui_notice_e n = notice_of(b->getKind());

        if (n > notice)
            notice = n;
    }

    ui_style_theme()->frame->set_notice(notice);
}
