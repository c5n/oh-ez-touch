/**
 * @file item_player.cpp
 *
 * Players: transport, with play the size it deserves, and what is playing.
 *
 * openHAB's Player item is the transport and nothing else -- PLAY, PAUSE, NEXT,
 * PREVIOUS. What is playing, and how loud, is never on it: every binding puts
 * those on items of their own (Sonos currenttitle and volume, Spotify
 * trackName, Squeezebox title, Kodi title, ...). So the screen takes them
 * from the tiles that share the player's Frame:
 *
 *     Frame label="Music" {
 *         Default item=Sonos_Control
 *         Text    item=Sonos_Title
 *         Text    item=Sonos_Artist
 *         Slider  item=Sonos_Volume
 *     }
 *
 * The first Text is the title, the others go on the line under it, and the
 * first Slider is the volume. A child block on the player would have been the
 * tidier way to say this, but openHAB's sitemap grammar refuses one on a
 * Switch or a Default. A Frame is plain openHAB and reads the same way in
 * Basic UI, and since its tiles are on the page they are already followed by
 * the event stream; the screen only has to look at them. A player outside
 * any Frame, or in one with nothing else in it, gets the transport alone, as
 * it always did.
 */
#include "item_screen.hpp"

#include "ui/ui_style.hpp"

#include <stdio.h>
#include <string.h>

/* Without anything else to show, play and pause are what anyone reaches for,
 * so they get the top of the screen to themselves and previous and next share
 * the row below. */
#define PRIMARY_H   80
#define SECONDARY_H 88

/* With a title or a volume, the three keys share one row. The sums, for the
 * 184 px the body has in landscape: 4 + 26 + 2 + 20 + 6 + 64 + 6 + 44 + 4 =
 * 176, the two text lines being whatever the theme's faces make them. */
#define TRANSPORT_H 64
#define VOLUME_H    44

/* The lines under the title: artist, album. More than that would not fit on
 * the one line they share anyway. */
#define SUBTITLE_MAX 2

#define SLOT_NONE 0xFFu

/* The tiles the screen shows besides its own, found when it is built. By slot
 * rather than by pointer, so that every look at one goes through the page's
 * own accessor. One player screen is open at a time; item_screen guarantees
 * it. */
static struct
{
    uint8_t   title;
    uint8_t   subtitle[SUBTITLE_MAX];
    size_t    subtitle_count;
    uint8_t   volume;
    lv_obj_t *title_label;
    lv_obj_t *subtitle_label;
} player;

static void primary_sync(struct item_view_s *v);

/* What a text tile shows, without the tile: the label openHAB maps the state
 * to, the value it formatted, or the state itself -- except for the NULL and
 * UNDEF of an item nobody has set, which are not a title. */
static const char *tile_text(Item *item, char *buffer, size_t buffer_size)
{
    const char *mapped = item->mappedLabel();

    if (mapped != NULL)
        return mapped;

    if (item->getTransformedStateText()[0] != '\0')
        return item->getTransformedStateText();

    const char *state = item->getStateText();

    if (strcmp(state, "NULL") == 0 || strcmp(state, "UNDEF") == 0)
        return "";

    /* A number's state is kept re-printed as "%f". */
    if (item->getType() == ItemType::type_number)
    {
        snprintf(buffer, buffer_size, "%g", (double)item->getStateNumber());
        return buffer;
    }

    return state;
}

/* The player's Frame-mates: the text tiles and the first slider, in page
 * order. */
static void siblings_find(struct item_view_s *v)
{
    memset(&player, 0, sizeof(player));
    player.title  = SLOT_NONE;
    player.volume = SLOT_NONE;

    uint8_t frame = v->item->getFrame();

    if (frame == 0)
        return;

    for (uint8_t slot = 0;; slot++)
    {
        Item *item = item_screen_page_item(slot);

        if (item == NULL)
            break;

        if (slot == v->slot || item->getFrame() != frame)
            continue;

        switch (item->getType())
        {
        case ItemType::type_string:
        case ItemType::type_number:
            if (player.title == SLOT_NONE)
                player.title = slot;
            else if (player.subtitle_count < SUBTITLE_MAX)
                player.subtitle[player.subtitle_count++] = slot;
            break;

        case ItemType::type_slider:
            /* Read-only is a level meter, not a volume. */
            if (player.volume == SLOT_NONE && item->isReadOnly() == false)
                player.volume = slot;
            break;

        default:
            break;
        }
    }
}

static void info_sync(void)
{
    char buffer[24];

    if (player.title_label != NULL)
    {
        Item *item = item_screen_page_item(player.title);

        lv_label_set_text(player.title_label,
                          (item != NULL) ? tile_text(item, buffer, sizeof(buffer)) : "");
    }

    if (player.subtitle_label != NULL)
    {
        /* Two of them at STR_TRANSFORMEDSTATE_TEXT_LEN, and the separator. */
        char line[2 * STR_TRANSFORMEDSTATE_TEXT_LEN + 4] = "";

        for (size_t i = 0; i < player.subtitle_count; i++)
        {
            Item *item = item_screen_page_item(player.subtitle[i]);

            if (item == NULL)
                continue;

            const char *text = tile_text(item, buffer, sizeof(buffer));

            if (text[0] == '\0')
                continue;

            if (line[0] != '\0')
                strlcat(line, " - ", sizeof(line));

            strlcat(line, text, sizeof(line));
        }

        lv_label_set_text(player.subtitle_label, line);
    }
}

static void volume_sync(struct item_view_s *v)
{
    lv_obj_t *field = v->extra[2];
    Item     *item  = item_screen_page_item(player.volume);

    /* Not under a finger: a state arriving mid drag would yank the knob away
     * from it. */
    if (field == NULL || item == NULL || lv_obj_has_state(field, LV_STATE_PRESSED))
        return;

    lv_slider_set_value(field, (int32_t)item->getStateNumber(), LV_ANIM_ON);
}

static void command_event(lv_event_t *e)
{
    struct item_view_s *v = (struct item_view_s *)lv_event_get_user_data(e);
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    const char *command = (const char *)lv_obj_get_user_data(btn);

    if (command == NULL)
        return;

    /* NEXT and PREVIOUS are commands, not states: a player answers them with
     * the PLAY it was already in. Setting them as the state showed "NEXT" on
     * the tile and turned the primary key back to play until it did. */
    if (strcmp(command, "PLAY") != 0 && strcmp(command, "PAUSE") != 0)
    {
        item_screen_send(v, command);
        return;
    }

    v->item->setStateText(command);
    item_screen_publish(v);

    /* And follow the state this tap just set, because the poll will not.
     *
     * refresh() runs only when a poll brings back something different from
     * what the item already holds -- and setStateText() above has just made
     * the item agree with what the server is about to report. So the key kept
     * whatever glyph and command it was built with: press play once and it
     * stayed play, sending PLAY to an item that was already playing, for as
     * long as the screen was open. Pausing from the panel was impossible.
     *
     * The slider does the same thing after a local change for the same reason;
     * the selection list marks its own row inline. This is the player's. */
    primary_sync(v);
}

static void volume_event(lv_event_t *e)
{
    struct item_view_s *v    = (struct item_view_s *)lv_event_get_user_data(e);
    Item               *item = item_screen_page_item(player.volume);
    char                text[16];

    if (item == NULL)
        return;

    int32_t value = lv_slider_get_value(v->extra[2]);

    openhab_format_number(text, sizeof(text), (float)value, 1.0f);
    item->setStateNumber((float)value);
    item_screen_send_to(item, player.volume, text);
}

static lv_obj_t *key_create(struct item_view_s *v, lv_obj_t *parent, const char *symbol,
                            const char *command)
{
    lv_obj_t *btn = item_screen_button(parent, symbol);

    lv_obj_set_user_data(btn, (void *)command);
    lv_obj_add_event_cb(btn, command_event, LV_EVENT_CLICKED, v);

    item_screen_glyph(btn);

    return btn;
}

/* PLAY when it is not playing, PAUSE when it is. One key rather than two,
 * because the state already says which one it would be. */
static void primary_sync(struct item_view_s *v)
{
    if (v->control == NULL)
        return;

    bool playing = (strcmp(v->item->getStateText(), "PLAY") == 0);
    lv_obj_t *label = lv_obj_get_child(v->control, 0);

    lv_obj_set_user_data(v->control, (void *)(playing ? "PAUSE" : "PLAY"));

    if (label != NULL)
        lv_label_set_text(label, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
}

static lv_obj_t *info_label(lv_obj_t *parent, lv_style_t *style)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_obj_add_style(label, style, LV_PART_MAIN);
    lv_obj_set_width(label, lv_pct(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    /* A title is as long as it is; this one goes round rather than being cut
     * off at the width of a 2.4" panel. */
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(label, "");

    return label;
}

/* Transport alone: the layout the screen always had. */
static void build_transport(struct item_view_s *v)
{
    v->control = key_create(v, v->body, LV_SYMBOL_PLAY, "PLAY");
    lv_obj_set_width(v->control, lv_pct(100));
    lv_obj_set_height(v->control, PRIMARY_H);

    lv_obj_t *row = item_screen_container(v->body);

    lv_obj_set_size(row, lv_pct(100), SECONDARY_H);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);

    v->extra[0] = key_create(v, row, LV_SYMBOL_PREV, "PREVIOUS");
    v->extra[1] = key_create(v, row, LV_SYMBOL_NEXT, "NEXT");

    for (int i = 0; i < 2; i++)
    {
        lv_obj_set_flex_grow(v->extra[i], 1);
        lv_obj_set_height(v->extra[i], lv_pct(100));
    }
}

/* What is playing, the keys in one row under it, and the volume. */
static void build_now_playing(struct item_view_s *v)
{
    if (player.title != SLOT_NONE)
    {
        player.title_label = info_label(v->body, &ui_style_label_state);

        if (player.subtitle_count > 0)
            player.subtitle_label = info_label(v->body, &ui_style_label);
    }

    lv_obj_t *row = item_screen_container(v->body);

    lv_obj_set_size(row, lv_pct(100), TRANSPORT_H);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);

    v->extra[0] = key_create(v, row, LV_SYMBOL_PREV, "PREVIOUS");
    v->control  = key_create(v, row, LV_SYMBOL_PLAY, "PLAY");
    v->extra[1] = key_create(v, row, LV_SYMBOL_NEXT, "NEXT");

    lv_obj_set_flex_grow(v->extra[0], 1);
    lv_obj_set_flex_grow(v->control, 2);
    lv_obj_set_flex_grow(v->extra[1], 1);

    lv_obj_set_height(v->extra[0], lv_pct(100));
    lv_obj_set_height(v->control, lv_pct(100));
    lv_obj_set_height(v->extra[1], lv_pct(100));

    Item *volume = item_screen_page_item(player.volume);

    if (volume != NULL)
    {
        v->extra[2] = item_screen_field(v->body, VOLUME_H);
        lv_slider_set_range(v->extra[2], (int32_t)volume->getMinVal(),
                            (int32_t)volume->getMaxVal());
        lv_slider_set_value(v->extra[2], (int32_t)volume->getStateNumber(), LV_ANIM_OFF);

        /* On release, like the slider screen: a command per pixel would
         * flood the queue and the volume would chase the finger. */
        lv_obj_add_event_cb(v->extra[2], volume_event, LV_EVENT_RELEASED, v);
    }
}

static void build(struct item_view_s *v)
{
    lv_obj_set_flex_flow(v->body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(v->body, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(v->body, 4, 0);
    lv_obj_set_style_pad_row(v->body, 6, 0);

    siblings_find(v);

    if (player.title == SLOT_NONE && player.volume == SLOT_NONE)
    {
        lv_obj_set_style_pad_all(v->body, 8, 0);
        lv_obj_set_style_pad_row(v->body, 8, 0);
        build_transport(v);
    }
    else
    {
        build_now_playing(v);
    }

    primary_sync(v);
    info_sync();
}

/* The player, or any tile of the page: follows_page is set, so a new title
 * lands here too. */
static void refresh(struct item_view_s *v)
{
    primary_sync(v);
    info_sync();
    volume_sync(v);
}

static void destroy(struct item_view_s *v)
{
    LV_UNUSED(v);

    memset(&player, 0, sizeof(player));
}

const struct item_screen_dsc_s item_screen_player = {
    ItemType::type_player, build, refresh, destroy, true};
