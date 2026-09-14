#ifndef UI_INFOLABEL_HPP
#define UI_INFOLABEL_HPP

#include <stdint.h>
#include <lvgl.h>

/* A message box shown over the UI, e.g. for WLAN state changes.
 *
 * Under LVGL v7 this was an lv_msgbox used purely as a styled text panel, and
 * for a while under v9 it was a plain object with one label -- because the v9
 * msgbox is a full dialog with a header, a footer and a button area, and there
 * was nothing to put in any of them.
 *
 * There is now: the button that folds the box away. A message with no timeout
 * ("WLAN NOT CONNECTED", "this sitemap will not load") used to sit in the
 * middle of the screen until the condition cleared, which on a panel whose
 * server is down means forever -- so the page underneath was unreachable for
 * exactly as long as the problem lasted. Folding it leaves the frame's notice
 * indicator behind, and that indicator is the way back to it.
 *
 * It lives on the top layer so it floats above the page without being deleted
 * when the page is rebuilt. */
class Infolabel
{
public:
    enum infolabel_type_e
    {
        INFO,
        WARNING,
        ERROR
    };

    /* Raise the box, or re-word the one that is up. `timeout` is in seconds;
     * 0 means it stays until something takes it down.
     *
     * Calling this with the same severity, topic and text as the box is
     * already showing does nothing at all: no sound, and a folded box stays
     * folded. See `said` below for why that is load-bearing. */
    void create(enum infolabel_type_e type, const char *topic, const char *text,
                uint16_t timeout);

    void destroy(void);
    void loop(void);

    /* Whether a box exists and what it says, for the simulator's control
     * interface. */
    bool isUp(void) const { return mb != NULL; }
    bool isFolded(void) const { return folded; }

    enum infolabel_type_e getKind(void) const { return kind; }

    const char *getTopic(void) const { return (mb != NULL) ? topic : NULL; }

    /* Off the widget, unlike the topic: the content label wraps rather than
     * dots, so what it holds is still what was passed -- and one caller passes
     * a sitemap URL, which is longer than any buffer worth having here. */
    const char *getText(void) const
    {
        return (body != NULL) ? lv_label_get_text(body) : NULL;
    }

    /* Unfold every box that is folded away. What the frame's notice indicator
     * does when it is touched; there is no per-box indicator because there is
     * no room in any family's chrome for two. */
    static void unfold(void);

    /* Hide the boxes while something is pushed over the page, and bring them
     * back when it is popped.
     *
     * This is not the same state as folded, and the two have to be kept apart:
     * a message that arrives while the settings screen is up must not be
     * counted as one the user has dismissed, and popping the settings screen
     * must not undo a fold the user meant. */
    static void cover(bool covered);

    /* Tell the current frame which severity to show, if any. Called whenever a
     * box appears or goes away, and again by chrome_create() -- a frame is
     * objects, so the family that has just been built knows nothing about a
     * box that was already up. */
    static void refresh_notice(void);

private:
    lv_obj_t *mb = NULL;    /* the lv_msgbox */
    lv_obj_t *title = NULL; /* its title-bar label -- the topic */
    lv_obj_t *body = NULL;  /* its content label   -- the text  */

    /* The one button, at the right-hand end of the title bar. Kept because
     * create() re-sizes it on every message; see the comment there. */
    lv_obj_t *fold_btn = NULL;

    /* The topic as it was given. The title bar is one line, so its label dots
     * a topic that does not fit -- and LV_LABEL_LONG_DOT writes those dots
     * into the label's own text, which is what getTopic() would otherwise
     * report. Long enough for every topic this firmware has, with room. */
    char topic[48] = {0};

    uint64_t timeout_timestamp = 0;

    /* The severity of the box on screen. Kept because it is the one thing
     * about a live box that is not readable back off the widgets. */
    enum infolabel_type_e kind = INFO;

    /* Whether the user folded it away. Cleared by destroy(), and by a create()
     * that has something new to say -- a box that has been folded is dismissed
     * for that message, not for every message after it. */
    bool folded = false;

    /* What the box on screen is *saying*, as a hash, so that saying it again
     * does not sound again.
     *
     * This is load-bearing rather than tidy. openhab_ui re-creates the sitemap
     * failure box on every retry, and main.cpp raises the identical "WLAN /
     * NOT CONNECTED" warning from two places one loop apart -- so without it a
     * panel that cannot reach its server would beep at the room every few
     * seconds, forever, and would unfold itself again every time the user put
     * it away. A hash rather than a copy of the strings: the text can be a
     * sitemap URL, and the comment on lv_label_set_text() in create() explains
     * why there is no buffer for one of those. */
    uint32_t said = 0;

    /* Set while a pushed screen covers the page. One flag for both boxes,
     * because there is one page and one thing that can cover it. */
    static bool covered;

    void build(void);
    void refresh(void) const;
    void fold(void);

    static void fold_event(lv_event_t *e);
};

/* The two boxes this firmware has, declared where the class is so that the
 * readers of both -- the simulator's control interface, which reports
 * whichever is up, and the statics above, which act on both -- do not have to
 * declare them themselves.
 *
 * They are separate instances on purpose: main.cpp owns the WLAN and setup
 * messages, openhab_ui.cpp owns "this sitemap will not load", and either can
 * be on screen without the other. */
extern Infolabel infolabel;            /* main.cpp */
extern Infolabel openhab_ui_infolabel; /* openhab_ui.cpp */

#endif
