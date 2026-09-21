#ifndef UI_GEOMETRY_HPP
#define UI_GEOMETRY_HPP

#include <stdbool.h>
#include <stdint.h>

/* Where the tiles go inside whatever rectangle a frame leaves.
 *
 * Deliberately free of every dependency, the way ui_theme.hpp is: no <lvgl.h>,
 * so the host tests can link it and check each family's grid against the size
 * floor without dragging LVGL into test/host/.
 *
 * The tile geometry used to be `lv_obj_get_width(parent) / 3 - 2` inside
 * widget_create(), which is fine as long as there is exactly one layout. There
 * are three now, and they differ in margin and gutter as well as in where the
 * rectangle starts. */

struct ui_grid_s
{
    uint8_t cols;
    uint8_t rows;
    uint8_t gutter; /* between tiles, both axes */
    uint8_t margin; /* between the tiles and the edge of the content rect */
};

struct ui_geom_rect_s
{
    int16_t x, y, w, h;
};

static inline int16_t ui_grid_cell_w(const struct ui_grid_s *g, int16_t area_w)
{
    if (g->cols == 0)
        return 0;

    return (int16_t)((area_w - 2 * g->margin - (g->cols - 1) * g->gutter) / g->cols);
}

static inline int16_t ui_grid_cell_h(const struct ui_grid_s *g, int16_t area_h)
{
    if (g->rows == 0)
        return 0;

    return (int16_t)((area_h - 2 * g->margin - (g->rows - 1) * g->gutter) / g->rows);
}

/* Cell `index` in reading order, relative to the content rectangle's origin.
 * False when the index is past the end of the grid, which is how a page with
 * more items than cells stops. */
static inline bool ui_grid_cell(const struct ui_grid_s *g, int16_t area_w, int16_t area_h,
                                uint8_t index, struct ui_geom_rect_s *out)
{
    if (g->cols == 0 || g->rows == 0 || index >= (uint16_t)(g->cols * g->rows))
        return false;

    int16_t w = ui_grid_cell_w(g, area_w);
    int16_t h = ui_grid_cell_h(g, area_h);

    if (w <= 0 || h <= 0)
        return false;

    uint8_t col = (uint8_t)(index % g->cols);
    uint8_t row = (uint8_t)(index / g->cols);

    out->x = (int16_t)(g->margin + col * (w + g->gutter));
    out->y = (int16_t)(g->margin + row * (h + g->gutter));
    out->w = w;
    out->h = h;

    return true;
}


/* ------------------------------------------------- a list that gains columns
 *
 * A column of equal-sized choices in a rectangle that may not be tall enough
 * for all of them -- the selection screen's mappings, of which a sitemap may
 * carry ten. The list used to scroll: choices past the fold were off screen
 * with nothing but a scrollbar to say so, on a panel where a drag is not a
 * gesture at all (see ui_input.c). A choice you cannot see is a choice you do
 * not have, so the list gains a column instead.
 *
 * Balanced rather than filled: seven choices in columns of three go 3+2+2 and
 * not 3+3+1. Filling each column before starting the next is what a text
 * layout does, and it leaves the last column nearly empty beside two full
 * ones, which reads as a mistake.
 *
 * Here rather than in the builder for the same reason the tile grid is: it is
 * arithmetic with an answer that can be wrong in ways nobody sees on a panel,
 * and this header drags in no LVGL, so the host tests can have it. */

struct ui_columns_s
{
    uint8_t cols;   /* how many columns the choices are spread over */
    uint8_t rows;   /* the tallest column's count -- what has to fit */
    int16_t item_h; /* what one choice gets, gaps already taken out */
};

/* `gap` is the space between two choices in a column, `item_min_h` the height
 * below which a choice stops being reachable with a finger, and `item_max_h`
 * the height past which it stops reading as a row of a list. Between the two
 * the choices stretch to fill the column, which is what makes three of them
 * look placed rather than dropped at the top. */
static inline struct ui_columns_s ui_columns_pack(uint8_t count, int16_t area_h,
                                                  int16_t item_min_h, int16_t item_max_h,
                                                  int16_t gap)
{
    struct ui_columns_s out = {0, 0, 0};

    if (count == 0 || area_h <= 0 || item_min_h <= 0)
        return out;

    /* How many fit in one column at the smallest a choice may be. At least
     * one, even in a rectangle too short for it: an unreachable row is still
     * better than a column with nothing in it. */
    int16_t per_col = (int16_t)((area_h + gap) / (item_min_h + gap));

    if (per_col < 1)
        per_col = 1;

    out.cols = (uint8_t)((count + per_col - 1) / per_col);
    out.rows = (uint8_t)((count + out.cols - 1) / out.cols);

    out.item_h = (int16_t)((area_h - (out.rows - 1) * gap) / out.rows);

    if (out.item_h > item_max_h)
        out.item_h = item_max_h;

    if (out.item_h < item_min_h)
        out.item_h = item_min_h;

    return out;
}

/* How many choices land in column `col`, and which one it starts at. The
 * remainder goes to the leftmost columns, so a column is never more than one
 * choice shorter than the one before it. */
static inline uint8_t ui_columns_count(uint8_t count, uint8_t cols, uint8_t col)
{
    if (cols == 0 || col >= cols)
        return 0;

    return (uint8_t)(count / cols + ((col < count % cols) ? 1 : 0));
}

static inline uint8_t ui_columns_first(uint8_t count, uint8_t cols, uint8_t col)
{
    if (cols == 0 || col >= cols)
        return count;

    uint8_t rem = (uint8_t)(count % cols);

    return (uint8_t)(col * (count / cols) + ((col < rem) ? col : rem));
}

#endif /* UI_GEOMETRY_HPP */
