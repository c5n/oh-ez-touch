/**
 * @file page_color.h
 *
 * The one colour value the page model knows by name. A connector turns the
 * server's colour rules into 0xRRGGBB (see openhab/openhab_color.h); the UI
 * draws the theme's colour for PAGE_COLOR_NONE.
 */
#ifndef PAGE_COLOR_H
#define PAGE_COLOR_H

/* No colour: the theme's own. Outside 24 bits, so no real colour is it. */
#define PAGE_COLOR_NONE 0xFFFFFFFFu

#endif /* PAGE_COLOR_H */
