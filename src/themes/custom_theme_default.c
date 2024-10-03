/**
 * @file lv_theme_template.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lvgl.h" /*To see all the widgets*/

//#include "lv_misc/lv_gc.h"

#if defined(LV_GC_INCLUDE)
    #include LV_GC_INCLUDE
#endif /* LV_ENABLE_GC */

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

typedef struct {
    lv_style_t bg;
    lv_style_t btn;
    lv_style_t round;
    lv_style_t color;
    lv_style_t color_grad;
    lv_style_t gray;
    lv_style_t tight;
    lv_style_t container;
    lv_style_t slider;
    lv_style_t slider_knob;
    lv_style_t win_header;
    lv_style_t cpicker;
    lv_style_t cpicker_knob;
} theme_styles_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void theme_apply(lv_obj_t * obj, lv_theme_style_t name);

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_theme_t theme;
static theme_styles_t * styles;

static bool inited;

/**********************
 *      MACROS
 **********************/

/**********************
 *   STATIC FUNCTIONS
 **********************/
static void style_init_reset(lv_style_t * style);


static void basic_init(void)
{
    style_init_reset(&styles->bg);
    lv_style_set_bg_opa(&styles->bg, LV_STATE_DEFAULT, LV_OPA_COVER);
    lv_style_set_radius(&styles->bg, LV_STATE_DEFAULT, LV_DPI / 15);
    lv_style_set_border_width(&styles->bg, LV_STATE_DEFAULT, 0);
    lv_style_set_line_width(&styles->bg, LV_STATE_DEFAULT, 1);
    lv_style_set_scale_end_line_width(&styles->bg, LV_STATE_DEFAULT, 1);
    lv_style_set_scale_end_color(&styles->bg, LV_STATE_DEFAULT, theme.color_primary);
    lv_style_set_text_font(&styles->bg, LV_STATE_DEFAULT, theme.font_small);
    lv_style_set_text_color(&styles->bg, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    lv_style_set_pad_left(&styles->bg, LV_STATE_DEFAULT, LV_DPI / 20);
    lv_style_set_pad_right(&styles->bg, LV_STATE_DEFAULT, LV_DPI / 20);
    lv_style_set_pad_top(&styles->bg, LV_STATE_DEFAULT, LV_DPI / 20);
    lv_style_set_pad_bottom(&styles->bg, LV_STATE_DEFAULT, LV_DPI / 20);
    lv_style_set_pad_inner(&styles->bg, LV_STATE_DEFAULT, LV_DPI / 20);

    style_init_reset(&styles->container);
    lv_style_set_pad_left(&styles->container, LV_STATE_DEFAULT, LV_DPI / 50);
    lv_style_set_pad_right(&styles->container, LV_STATE_DEFAULT, LV_DPI / 50);
    lv_style_set_pad_top(&styles->container, LV_STATE_DEFAULT, LV_DPI / 50);
    lv_style_set_pad_bottom(&styles->container, LV_STATE_DEFAULT, LV_DPI / 50);
    lv_style_set_pad_inner(&styles->container, LV_STATE_DEFAULT, LV_DPI / 50);
    //lv_style_set_bg_opa(&styles->container, LV_STATE_DEFAULT, LV_OPA_TRANSP);

    style_init_reset(&styles->btn);
    lv_style_set_radius(&styles->btn, LV_STATE_DEFAULT, LV_DPI / 15);
    lv_style_set_border_color(&styles->btn, LV_STATE_DEFAULT, lv_color_make(0x0b, 0x19, 0x28));
    lv_style_set_border_width(&styles->btn, LV_STATE_DEFAULT, LV_DPI / 50 >= 1 ? LV_DPI / 50 : 1);
    lv_style_set_border_opa(&styles->btn, LV_STATE_DEFAULT, LV_OPA_70);
    lv_style_set_bg_color(&styles->btn, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_style_set_bg_grad_color(&styles->btn, LV_STATE_DEFAULT, LV_COLOR_SILVER);
    lv_style_set_bg_grad_dir(&styles->btn, LV_STATE_DEFAULT, LV_GRAD_DIR_VER);
    lv_style_set_text_color(&styles->btn, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    lv_style_set_shadow_color(&styles->btn, LV_STATE_DEFAULT, LV_COLOR_GRAY);
    lv_style_set_shadow_ofs_y(&styles->btn, LV_STATE_DEFAULT, 6);
    lv_style_set_shadow_width(&styles->btn, LV_STATE_DEFAULT, 0);
    lv_style_set_text_font(&styles->btn, LV_STATE_DEFAULT, theme.font_small);

    lv_style_set_bg_color(&styles->btn, LV_STATE_PRESSED, lv_color_darken(theme.color_primary, LV_OPA_60));
    lv_style_set_bg_grad_color(&styles->btn, LV_STATE_PRESSED, lv_color_darken(theme.color_primary, LV_OPA_30));
    lv_style_set_bg_grad_dir(&styles->btn, LV_STATE_PRESSED, LV_GRAD_DIR_VER);
    lv_style_set_text_color(&styles->btn, LV_STATE_PRESSED, LV_COLOR_SILVER);

    lv_style_set_bg_color(&styles->btn, LV_STATE_DISABLED, LV_COLOR_SILVER);
    lv_style_set_text_color(&styles->btn, LV_STATE_DISABLED, LV_COLOR_GRAY);
    lv_style_set_image_recolor(&styles->btn, LV_STATE_DISABLED, LV_COLOR_GRAY);

    lv_style_set_pad_left(&styles->btn, LV_STATE_DEFAULT, LV_DPI / 6);
    lv_style_set_pad_right(&styles->btn, LV_STATE_DEFAULT, LV_DPI / 6);
    lv_style_set_pad_top(&styles->btn, LV_STATE_DEFAULT, LV_DPI / 6);
    lv_style_set_pad_bottom(&styles->btn, LV_STATE_DEFAULT, LV_DPI / 6);
    lv_style_set_pad_inner(&styles->btn, LV_STATE_DEFAULT, LV_DPI / 12);


    style_init_reset(&styles->round);
    lv_style_set_radius(&styles->round, LV_STATE_DEFAULT, LV_RADIUS_CIRCLE);

    style_init_reset(&styles->color);
    lv_style_set_bg_color(&styles->color, LV_STATE_DEFAULT, theme.color_primary);
    lv_style_set_line_color(&styles->color, LV_STATE_DEFAULT, theme.color_primary);

    style_init_reset(&styles->color_grad);
    lv_style_set_bg_color(&styles->color_grad, LV_STATE_DEFAULT, lv_color_darken(theme.color_primary, LV_OPA_30));
    lv_style_set_bg_grad_color(&styles->color_grad, LV_STATE_DEFAULT, lv_color_darken(theme.color_primary, LV_OPA_60));
    lv_style_set_bg_grad_dir(&styles->color_grad, LV_STATE_DEFAULT, LV_GRAD_DIR_VER);
    lv_style_set_line_color(&styles->color_grad, LV_STATE_DEFAULT, theme.color_primary);

    style_init_reset(&styles->gray);
    lv_style_set_bg_color(&styles->gray, LV_STATE_DEFAULT, LV_COLOR_SILVER);
    lv_style_set_line_color(&styles->gray, LV_STATE_DEFAULT, LV_COLOR_SILVER);
    lv_style_set_text_color(&styles->gray, LV_STATE_DEFAULT, LV_COLOR_GRAY);

    style_init_reset(&styles->tight);
    lv_style_set_pad_left(&styles->tight, LV_STATE_DEFAULT, 0);
    lv_style_set_pad_right(&styles->tight, LV_STATE_DEFAULT, 0);
    lv_style_set_pad_top(&styles->tight, LV_STATE_DEFAULT, 0);
    lv_style_set_pad_bottom(&styles->tight, LV_STATE_DEFAULT, 0);
    lv_style_set_pad_inner(&styles->tight, LV_STATE_DEFAULT, 0);
}

static void bar_init(void)
{
#if LV_USE_BAR

#endif
}

static void btn_init(void)
{
#if LV_USE_BTN != 0

#endif
}

static void btnmatrix_init(void)
{
#if LV_USE_BTNMATRIX

#endif
}

static void cpicker_init(void)
{
#if LV_USE_CPICKER
    style_init_reset(&styles->cpicker);
    lv_style_set_bg_opa(&styles->cpicker, LV_STATE_DEFAULT, LV_OPA_COVER);
    lv_style_set_value_opa(&styles->cpicker, LV_STATE_DEFAULT, LV_OPA_COVER);
    lv_style_set_scale_width(&styles->cpicker, LV_STATE_DEFAULT, LV_DPI / 3);
    lv_style_set_pad_inner(&styles->cpicker, LV_STATE_DEFAULT, LV_DPI / 20);

    style_init_reset(&styles->cpicker_knob);
    lv_style_set_bg_opa(&styles->cpicker_knob, LV_STATE_DEFAULT, LV_OPA_COVER);
    lv_style_set_border_color(&styles->cpicker_knob, LV_STATE_DEFAULT, lv_color_make(0x0b, 0x19, 0x28));
    lv_style_set_border_width(&styles->cpicker_knob, LV_STATE_DEFAULT, LV_DPI / 50 >= 1 ? LV_DPI / 50 : 1);
    lv_style_set_border_opa(&styles->cpicker_knob, LV_STATE_DEFAULT, LV_OPA_COVER);
    lv_style_set_pad_top(&styles->cpicker_knob, LV_STATE_DEFAULT, LV_DPI / 20);
    lv_style_set_pad_bottom(&styles->cpicker_knob, LV_STATE_DEFAULT, LV_DPI / 20);
    lv_style_set_pad_left(&styles->cpicker_knob, LV_STATE_DEFAULT, LV_DPI / 20);
    lv_style_set_pad_right(&styles->cpicker_knob, LV_STATE_DEFAULT, LV_DPI / 20);
#endif
}

static void cont_init(void)
{
#if LV_USE_CONT != 0

#endif
}

static void img_init(void)
{
#if LV_USE_IMG != 0

#endif
}

static void label_init(void)
{
#if LV_USE_LABEL != 0

#endif
}

static void page_init(void)
{
#if LV_USE_PAGE

#endif
}

static void slider_init(void)
{
#if LV_USE_SLIDER != 0

    style_init_reset(&styles->slider);
    lv_style_set_radius(&styles->slider, LV_STATE_DEFAULT, LV_DPI / 20);
    lv_style_set_border_color(&styles->slider, LV_STATE_DEFAULT, lv_color_make(0x0b, 0x19, 0x28));
    lv_style_set_border_width(&styles->slider, LV_STATE_DEFAULT, LV_DPI / 50 >= 1 ? LV_DPI / 50 : 1);
    lv_style_set_border_opa(&styles->slider, LV_STATE_DEFAULT, LV_OPA_70);
    lv_style_set_bg_opa(&styles->slider, LV_STATE_DEFAULT, LV_OPA_COVER);
    lv_style_set_bg_color(&styles->slider, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_style_set_bg_grad_color(&styles->slider, LV_STATE_DEFAULT, LV_COLOR_SILVER);
    lv_style_set_bg_grad_dir(&styles->slider, LV_STATE_DEFAULT, LV_GRAD_DIR_VER);
    lv_style_set_pad_top(&styles->slider, LV_STATE_DEFAULT, LV_DPI / 20);
    lv_style_set_pad_bottom(&styles->slider, LV_STATE_DEFAULT, LV_DPI / 20);
    lv_style_set_pad_left(&styles->slider, LV_STATE_DEFAULT, LV_DPI / 20);
    lv_style_set_pad_right(&styles->slider, LV_STATE_DEFAULT, LV_DPI / 20);


    style_init_reset(&styles->slider_knob);
    lv_style_set_radius(&styles->slider_knob, LV_STATE_DEFAULT, LV_DPI / 20);
    lv_style_set_border_color(&styles->slider_knob, LV_STATE_DEFAULT, lv_color_make(0x0b, 0x19, 0x28));
    lv_style_set_border_width(&styles->slider_knob, LV_STATE_DEFAULT, LV_DPI / 50 >= 1 ? LV_DPI / 50 : 1);
    lv_style_set_border_opa(&styles->slider_knob, LV_STATE_DEFAULT, LV_OPA_70);
    lv_style_set_bg_opa(&styles->slider_knob, LV_STATE_DEFAULT, LV_OPA_COVER);
    lv_style_set_bg_color(&styles->slider_knob, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_style_set_bg_grad_color(&styles->slider_knob, LV_STATE_DEFAULT, LV_COLOR_SILVER);
    lv_style_set_bg_grad_dir(&styles->slider_knob, LV_STATE_DEFAULT, LV_GRAD_DIR_VER);



#endif
}

static void msgbox_init(void)
{
#if LV_USE_MSGBOX

#endif
}

static void table_init(void)
{
#if LV_USE_TABLE != 0

#endif
}

static void win_init(void)
{
#if LV_USE_WIN != 0
    style_init_reset(&styles->win_header);
    lv_style_set_radius(&styles->win_header, LV_STATE_DEFAULT, 5);
    lv_style_set_bg_color(&styles->win_header, LV_STATE_DEFAULT, theme.color_primary);
    lv_style_set_bg_opa(&styles->win_header, LV_STATE_DEFAULT, LV_OPA_COVER);
    lv_style_set_bg_grad_dir(&styles->win_header, LV_STATE_DEFAULT, LV_GRAD_DIR_NONE);
    lv_style_set_text_font(&styles->win_header, LV_STATE_DEFAULT, theme.font_normal);
    lv_style_set_text_color(&styles->win_header, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_style_set_pad_left(&styles->win_header, LV_STATE_DEFAULT, 10);
    lv_style_set_pad_right(&styles->win_header, LV_STATE_DEFAULT, 10);
    lv_style_set_pad_top(&styles->win_header, LV_STATE_DEFAULT, 5);
    lv_style_set_pad_bottom(&styles->win_header, LV_STATE_DEFAULT, 5);
#endif
}


/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * Initialize the default
 * @param color_primary the primary color of the theme
 * @param color_secondary the secondary color for the theme
 * @param flags ORed flags starting with `LV_THEME_DEF_FLAG_...`
 * @param font_small pointer to a small font
 * @param font_normal pointer to a normal font
 * @param font_subtitle pointer to a large font
 * @param font_title pointer to a extra large font
 * @return a pointer to reference this theme later
 */
lv_theme_t * custom_theme_default_init(lv_color_t color_primary, lv_color_t color_secondary, uint32_t flags,
                                    const lv_font_t * font_small, const lv_font_t * font_normal, const lv_font_t * font_subtitle,
                                    const lv_font_t * font_title)
{

    /* This trick is required only to avoid the garbage collection of
     * styles' data if LVGL is used in a binding (e.g. Micropython)
     * In a general case styles could be simple `static lv_style_t my style` variables or allocated directly into `styles`*/
    if(!inited) {
#if defined(LV_GC_INCLUDE)
        LV_GC_ROOT(_lv_theme_template_styles) = lv_mem_alloc(sizeof(theme_styles_t));
        styles = (theme_styles_t *)LV_GC_ROOT(_lv_theme_template_styles);
#else
        styles = lv_mem_alloc(sizeof(theme_styles_t));
#endif

    }

    theme.color_primary = color_primary;
    theme.color_secondary = color_secondary;
    theme.font_small = font_small;
    theme.font_normal = font_normal;
    theme.font_subtitle = font_subtitle;
    theme.font_title = font_title;
    theme.flags = flags;

    basic_init();
    cont_init();
    btn_init();
    label_init();
    bar_init();
    img_init();
    slider_init();
    cpicker_init();
    btnmatrix_init();
    msgbox_init();
    page_init();
    table_init();
    win_init();

    theme.apply_xcb = theme_apply;

    return &theme;
}


void theme_apply(lv_obj_t * obj, lv_theme_style_t name)
{
    lv_style_list_t * list;

    switch(name) {
        case LV_THEME_NONE:
            break;

        case LV_THEME_SCR:
            lv_obj_clean_style_list(obj, LV_OBJ_PART_MAIN);
            list = lv_obj_get_style_list(obj, LV_OBJ_PART_MAIN);
            _lv_style_list_add_style(list, &styles->bg);
            _lv_style_list_add_style(list, &styles->tight);
            break;
        case LV_THEME_OBJ:
            lv_obj_clean_style_list(obj, LV_OBJ_PART_MAIN);
            list = lv_obj_get_style_list(obj, LV_OBJ_PART_MAIN);
            _lv_style_list_add_style(list, &styles->bg);
            break;
#if LV_USE_CONT
        case LV_THEME_CONT:
            lv_obj_clean_style_list(obj, LV_OBJ_PART_MAIN);
            list = lv_obj_get_style_list(obj, LV_CONT_PART_MAIN);
            _lv_style_list_add_style(list, &styles->container);
            break;
#endif

#if LV_USE_BTN
        case LV_THEME_BTN:
            lv_obj_clean_style_list(obj, LV_BTN_PART_MAIN);
            list = lv_obj_get_style_list(obj, LV_BTN_PART_MAIN);
            _lv_style_list_add_style(list, &styles->bg);
            _lv_style_list_add_style(list, &styles->btn);
            break;
#endif

#if LV_USE_BTNMATRIX
        case LV_THEME_BTNMATRIX:
            lv_obj_clean_style_list(obj, LV_BTNMATRIX_PART_BG);
            list = lv_obj_get_style_list(obj, LV_BTNMATRIX_PART_BG);
            _lv_style_list_add_style(list, &styles->bg);

            lv_obj_clean_style_list(obj, LV_BTNMATRIX_PART_BTN);
            list = lv_obj_get_style_list(obj, LV_BTNMATRIX_PART_BTN);
            _lv_style_list_add_style(list, &styles->bg);
            _lv_style_list_add_style(list, &styles->btn);
            break;
#endif

#if LV_USE_BAR
        case LV_THEME_BAR:
            lv_obj_clean_style_list(obj, LV_BAR_PART_BG);
            list = lv_obj_get_style_list(obj, LV_BAR_PART_BG);
            _lv_style_list_add_style(list, &styles->bg);
            _lv_style_list_add_style(list, &styles->tight);

            lv_obj_clean_style_list(obj, LV_BAR_PART_INDIC);
            list = lv_obj_get_style_list(obj, LV_BAR_PART_INDIC);
            _lv_style_list_add_style(list, &styles->bg);
            _lv_style_list_add_style(list, &styles->color);
            break;
#endif

#if LV_USE_IMG
        case LV_THEME_IMAGE:
            lv_obj_clean_style_list(obj, LV_IMG_PART_MAIN);
            list = lv_obj_get_style_list(obj, LV_IMG_PART_MAIN);
            break;
#endif

#if LV_USE_IMGBTN
        case LV_THEME_IMGBTN:
            lv_obj_clean_style_list(obj, LV_IMG_PART_MAIN);
            list = lv_obj_get_style_list(obj, LV_IMG_PART_MAIN);
            break;
#endif

#if LV_USE_LABEL
        case LV_THEME_LABEL:
            lv_obj_clean_style_list(obj, LV_LABEL_PART_MAIN);
            list = lv_obj_get_style_list(obj, LV_LABEL_PART_MAIN);
            break;
#endif

#if LV_USE_SLIDER
        case LV_THEME_SLIDER:
            lv_obj_clean_style_list(obj, LV_SLIDER_PART_BG);
            list = lv_obj_get_style_list(obj, LV_SLIDER_PART_BG);
            _lv_style_list_add_style(list, &styles->slider);

            lv_obj_clean_style_list(obj, LV_SLIDER_PART_INDIC);
            list = lv_obj_get_style_list(obj, LV_SLIDER_PART_INDIC);
            _lv_style_list_add_style(list, &styles->slider);
            _lv_style_list_add_style(list, &styles->color_grad);

            lv_obj_clean_style_list(obj, LV_SLIDER_PART_KNOB);
            list = lv_obj_get_style_list(obj, LV_SLIDER_PART_KNOB);
            _lv_style_list_add_style(list, &styles->slider_knob);
            break;
#endif

#if LV_USE_MSGBOX
        case LV_THEME_MSGBOX:
            lv_obj_clean_style_list(obj, LV_MSGBOX_PART_BG);
            list = lv_obj_get_style_list(obj, LV_MSGBOX_PART_BG);
            _lv_style_list_add_style(list, &styles->bg);
            break;

        case LV_THEME_MSGBOX_BTNS:
            lv_obj_clean_style_list(obj, LV_MSGBOX_PART_BTN_BG);
            list = lv_obj_get_style_list(obj, LV_MSGBOX_PART_BTN_BG);
            _lv_style_list_add_style(list, &styles->bg);

            lv_obj_clean_style_list(obj, LV_MSGBOX_PART_BTN);
            list = lv_obj_get_style_list(obj, LV_MSGBOX_PART_BTN);
            _lv_style_list_add_style(list, &styles->bg);
            _lv_style_list_add_style(list, &styles->btn);
            break;

#endif

#if LV_USE_PAGE
        case LV_THEME_PAGE:
            lv_obj_clean_style_list(obj, LV_PAGE_PART_BG);
            list = lv_obj_get_style_list(obj, LV_PAGE_PART_BG);
            _lv_style_list_add_style(list, &styles->bg);
            _lv_style_list_add_style(list, &styles->gray);

            lv_obj_clean_style_list(obj, LV_PAGE_PART_SCROLLABLE);
            list = lv_obj_get_style_list(obj, LV_PAGE_PART_SCROLLABLE);
            _lv_style_list_add_style(list, &styles->bg);

            lv_obj_clean_style_list(obj, LV_PAGE_PART_SCROLLBAR);
            list = lv_obj_get_style_list(obj, LV_PAGE_PART_SCROLLBAR);
            _lv_style_list_add_style(list, &styles->bg);
            break;
#endif

#if LV_USE_OBJMASK
        case LV_THEME_OBJMASK:
            lv_obj_clean_style_list(obj, LV_OBJMASK_PART_MAIN);
            list = lv_obj_get_style_list(obj, LV_OBJMASK_PART_MAIN);
            break;
#endif

#if LV_USE_TABLE
        case LV_THEME_TABLE:
            lv_obj_clean_style_list(obj, LV_TABLE_PART_BG);
            list = lv_obj_get_style_list(obj, LV_TABLE_PART_BG);
            _lv_style_list_add_style(list, &styles->bg);

            lv_obj_clean_style_list(obj, LV_TABLE_PART_CELL1);
            list = lv_obj_get_style_list(obj, LV_TABLE_PART_CELL1);
            _lv_style_list_add_style(list, &styles->bg);

            lv_obj_clean_style_list(obj, LV_TABLE_PART_CELL2);
            list = lv_obj_get_style_list(obj, LV_TABLE_PART_CELL2);
            _lv_style_list_add_style(list, &styles->bg);

            lv_obj_clean_style_list(obj, LV_TABLE_PART_CELL3);
            list = lv_obj_get_style_list(obj, LV_TABLE_PART_CELL3);
            _lv_style_list_add_style(list, &styles->bg);

            lv_obj_clean_style_list(obj, LV_TABLE_PART_CELL4);
            list = lv_obj_get_style_list(obj, LV_TABLE_PART_CELL4);
            _lv_style_list_add_style(list, &styles->bg);
            break;
#endif

#if LV_USE_WIN
        case LV_THEME_WIN:
            lv_obj_clean_style_list(obj, LV_WIN_PART_BG);
            list = lv_obj_get_style_list(obj, LV_WIN_PART_BG);
            _lv_style_list_add_style(list, &styles->bg);

            lv_obj_clean_style_list(obj, LV_WIN_PART_SCROLLBAR);
            list = lv_obj_get_style_list(obj, LV_WIN_PART_SCROLLBAR);
            _lv_style_list_add_style(list, &styles->bg);

            lv_obj_clean_style_list(obj, LV_WIN_PART_CONTENT_SCROLLABLE);
            list = lv_obj_get_style_list(obj, LV_WIN_PART_CONTENT_SCROLLABLE);
            _lv_style_list_add_style(list, &styles->bg);

            lv_obj_clean_style_list(obj, LV_WIN_PART_HEADER);
            list = lv_obj_get_style_list(obj, LV_WIN_PART_HEADER);
            _lv_style_list_add_style(list, &styles->win_header);
            break;

        case LV_THEME_WIN_BTN:
            lv_obj_clean_style_list(obj, LV_BTN_PART_MAIN);
            list = lv_obj_get_style_list(obj, LV_BTN_PART_MAIN);
            _lv_style_list_add_style(list, &styles->bg);
            _lv_style_list_add_style(list, &styles->btn);
            break;
#endif

#if LV_USE_CPICKER
        case LV_THEME_CPICKER:
            lv_obj_clean_style_list(obj, LV_CPICKER_PART_MAIN);
            list = lv_obj_get_style_list(obj, LV_CPICKER_PART_MAIN);
            _lv_style_list_add_style(list, &styles->cpicker);

            lv_obj_clean_style_list(obj, LV_CPICKER_PART_KNOB);
            list = lv_obj_get_style_list(obj, LV_CPICKER_PART_KNOB);
            _lv_style_list_add_style(list, &styles->cpicker_knob);
            break;
#endif

        default:
            break;
    }


    lv_obj_refresh_style(obj, LV_STYLE_PROP_ALL);


}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void style_init_reset(lv_style_t * style)
{
    if(inited) lv_style_reset(style);
    else lv_style_init(style);
}
