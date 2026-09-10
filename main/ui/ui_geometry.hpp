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

#endif /* UI_GEOMETRY_HPP */
