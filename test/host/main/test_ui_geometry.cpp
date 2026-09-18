/* Unit tests for the tile grid solver in ui_geometry.hpp.
 *
 * The tile geometry used to be `lv_obj_get_width(parent) / 3 - 2` inside
 * widget_create(), which needed no test because there was one layout and you
 * could see it. There are three now -- each family's frame leaves a different
 * rectangle, and each packs it with a different margin and gutter -- and the
 * one thing that must hold across all of them is a floor:
 *
 *   the user asked for tiles no smaller than 70% of the 104 x 101 they were,
 *   which is 73 x 71,
 *
 * and that is a promise about a physical target on glass rather than a detail
 * of the layout. A frame redesigned a few pixels too generously would break it
 * silently, and it would only show up as a panel that is harder to hit.
 *
 * Host-only, and that is by construction: ui_geometry.hpp includes no <lvgl.h>
 * for exactly this reason, the same way ui_theme.hpp does not. Run with:
 *   cd test/host && idf.py build && ./build/oh-ez-touch-host-test.elf
 */

#include <unity.h>

#include "test_suites.hpp"
#include "ui/ui_geometry.hpp"

/* The floor, and where it comes from. */
#define TILE_MIN_W 73
#define TILE_MIN_H 71

/* Each family's grid, and the content rectangle its frame leaves at 320x240.
 * These have to be kept in step with ui_style.cpp's FRAME() rows and with the
 * frames themselves; that they are written out twice is the point, because a
 * change to either that nobody meant will fail here. */
struct family_case_s
{
    const char      *name;
    struct ui_grid_s grid;
    int16_t          area_w;
    int16_t          area_h;
};

static const struct family_case_s families[] = {
    /* Material: a 30 px band of bare ground, then the grid edge to edge. */
    {"Material", {3, 2, 8, 8}, 320, 240 - 30},
    /* LCARS: inset in the crook of the elbow, (52,42) to (315,235). */
    {"LCARS", {3, 2, 6, 0}, 264, 194},
    /* Reticle: between the two hairlines, 22 px strip and 18 px rail. */
    {"Reticle", {3, 2, 6, 6}, 320, 240 - 22 - 1 - 18},
};

#define FAMILY_COUNT (sizeof(families) / sizeof(families[0]))

static void test_every_family_clears_the_finger_floor(void)
{
    for (size_t i = 0; i < FAMILY_COUNT; i++)
    {
        const struct family_case_s *f = &families[i];
        int16_t w = ui_grid_cell_w(&f->grid, f->area_w);
        int16_t h = ui_grid_cell_h(&f->grid, f->area_h);

        UNITY_TEST_ASSERT(w >= TILE_MIN_W, __LINE__, f->name);
        UNITY_TEST_ASSERT(h >= TILE_MIN_H, __LINE__, f->name);
    }
}

/* Six cells, and the last one has to end inside the rectangle. An off-by-one
 * here is a tile hanging off the bottom of the glass. */
static void test_every_family_fits_its_rectangle(void)
{
    for (size_t i = 0; i < FAMILY_COUNT; i++)
    {
        const struct family_case_s *f = &families[i];

        for (uint8_t slot = 0; slot < f->grid.cols * f->grid.rows; slot++)
        {
            struct ui_geom_rect_s cell;

            UNITY_TEST_ASSERT(ui_grid_cell(&f->grid, f->area_w, f->area_h, slot, &cell),
                              __LINE__, f->name);
            UNITY_TEST_ASSERT(cell.x >= 0 && cell.y >= 0, __LINE__, f->name);
            UNITY_TEST_ASSERT(cell.x + cell.w <= f->area_w, __LINE__, f->name);
            UNITY_TEST_ASSERT(cell.y + cell.h <= f->area_h, __LINE__, f->name);
        }
    }
}

/* Reading order, which the entrance stagger depends on: index 1 is to the
 * right of index 0, and index `cols` is below it. */
static void test_cells_are_in_reading_order(void)
{
    struct ui_grid_s      g = {3, 2, 6, 4};
    struct ui_geom_rect_s a, b, c;

    TEST_ASSERT_TRUE(ui_grid_cell(&g, 300, 200, 0, &a));
    TEST_ASSERT_TRUE(ui_grid_cell(&g, 300, 200, 1, &b));
    TEST_ASSERT_TRUE(ui_grid_cell(&g, 300, 200, 3, &c));

    TEST_ASSERT_GREATER_THAN_INT(a.x, b.x);
    TEST_ASSERT_EQUAL_INT(a.y, b.y);
    TEST_ASSERT_EQUAL_INT(a.x, c.x);
    TEST_ASSERT_GREATER_THAN_INT(a.y, c.y);
}

/* Past the end is false rather than a cell off the edge -- that is what stops
 * page_rebuild() when a sitemap page has more items than the grid has room
 * for. */
static void test_an_index_past_the_end_is_refused(void)
{
    struct ui_grid_s      g = {3, 2, 6, 4};
    struct ui_geom_rect_s cell;

    TEST_ASSERT_TRUE(ui_grid_cell(&g, 300, 200, 5, &cell));
    TEST_ASSERT_FALSE(ui_grid_cell(&g, 300, 200, 6, &cell));
    TEST_ASSERT_FALSE(ui_grid_cell(&g, 300, 200, 200, &cell));
}

/* A rectangle too small to hold the grid says so instead of returning a
 * negative width that would then be handed to lv_obj_set_size(). */
static void test_an_impossible_rectangle_is_refused(void)
{
    struct ui_grid_s      g = {3, 2, 6, 8};
    struct ui_geom_rect_s cell;

    TEST_ASSERT_FALSE(ui_grid_cell(&g, 20, 200, 0, &cell));
    TEST_ASSERT_FALSE(ui_grid_cell(&g, 300, 10, 0, &cell));
}

/* A grid with no columns or no rows is a table row someone half-filled in. */
static void test_a_degenerate_grid_is_refused(void)
{
    struct ui_grid_s      none = {0, 0, 0, 0};
    struct ui_geom_rect_s cell;

    TEST_ASSERT_FALSE(ui_grid_cell(&none, 320, 240, 0, &cell));
    TEST_ASSERT_EQUAL_INT(0, ui_grid_cell_w(&none, 320));
    TEST_ASSERT_EQUAL_INT(0, ui_grid_cell_h(&none, 240));
}

void test_ui_geometry_run(void)
{
    RUN_TEST(test_every_family_clears_the_finger_floor);
    RUN_TEST(test_every_family_fits_its_rectangle);
    RUN_TEST(test_cells_are_in_reading_order);
    RUN_TEST(test_an_index_past_the_end_is_refused);
    RUN_TEST(test_an_impossible_rectangle_is_refused);
    RUN_TEST(test_a_degenerate_grid_is_refused);
}
