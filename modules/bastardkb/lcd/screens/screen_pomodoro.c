#include QMK_KEYBOARD_H
#include "introspection.h"
#include "lvgl.h"
#include <ctype.h>

#include "lcd.h"
#include "menu.h"
#include "screen_base.h"
#include "screen_pomodoro.h"
#include "ui_elements.h"
#include "utilities.h"


LV_FONT_DECLARE(jetbrainsmonoextrabold60);

typedef struct
{
    lv_obj_t* obj;
    void (*update_function)(lv_obj_t* obj);
} obj_update_dilemma_pomodoro_status_t;

static void load_screen_pomodoro_base(void);
static void load_screen_pomodoro_menu(void);
static void menu_pomodoro_go_base(void);
static void menu_pomodoro_start_25(void);
static void menu_pomodoro_play_pause(void);
static void menu_pomodoro_start_10(void);
static lv_obj_t* ui_create_pomodoro_title(lv_obj_t* cont);
static lv_obj_t* ui_create_pomodoro_time(lv_obj_t* cont);
// static lv_obj_t* ui_create_pomodoro_arc(lv_obj_t* cont);
// static void update_pomodoro_bar(lv_obj_t* obj);
static void update_pomodoro_time(lv_obj_t* obj);

static lv_obj_t* ui_screen_pomodoro;
static lv_obj_t* ui_screen_pomodoro_menu;

static obj_update_dilemma_pomodoro_status_t widgets[3];
static obj_update_dilemma_menu_t menus[5];
static uint8_t menu_index = 0;
static uint8_t screen_index = 0;

static uint32_t timer_start = 0;
// TODO add config options for this
static uint32_t timer_max = 3 * 60 * 1000; // default 3 minutes for now

static bool timer_is_running = false;

// TODO update things here only if this module is loaded.

void load_module_pomodoro(void) { load_screen_pomodoro_base(); }

// TODO isolate the ui_screen_base into this folder, and instead return a
// pointer to it with this function?
void init_screen_pomodoro(void) {
    ui_screen_pomodoro = lv_obj_create(NULL);
    lv_obj_t* cont = ui_create_container(ui_screen_pomodoro);

    ui_screen_pomodoro_menu = lv_obj_create(NULL);
    lv_obj_t* cont_menu = ui_create_container(ui_screen_pomodoro_menu);

    /* ----- Widgets ----- */
    widgets[0] = (obj_update_dilemma_pomodoro_status_t){
        ui_create_pomodoro_title(cont),
        NULL,
    };
    widgets[1] = (obj_update_dilemma_pomodoro_status_t){
        ui_create_pomodoro_time(cont),
        &update_pomodoro_time,
    };
    // widgets[2] = (obj_update_dilemma_pomodoro_status_t){
    //     ui_create_progress_bar(cont, 4),
    //     &update_pomodoro_bar,
    // };

    /* ----- menus ----- */
    menus[0] = (obj_update_dilemma_menu_t){
        ui_create_menu_line(cont_menu, "Back to pomodoro"),
        NULL,
    };
    menus[1] = (obj_update_dilemma_menu_t){
        ui_create_menu_line(cont_menu, "Start 25 minutes"),
        &menu_pomodoro_start_25,
    };
    menus[2] = (obj_update_dilemma_menu_t){
        ui_create_menu_line(cont_menu, "Play / Pause"),
        &menu_pomodoro_play_pause,
    };
    menus[3] = (obj_update_dilemma_menu_t){
        ui_create_menu_line(cont_menu, "Start 10 minutes"),
        &menu_pomodoro_start_10,
    };
    menus[4] = (obj_update_dilemma_menu_t){
        ui_create_menu_line(cont_menu, "< Back to Main"),
        &menu_pomodoro_go_base,
    };
}

static void menu_pomodoro_go_base(void) {
    set_current_module(MODULE_BASE);
}

static void menu_pomodoro_start_25(void) {
    timer_start = timer_read32();
    timer_max = 25 * 60 * 1000;
    timer_is_running = true;
}

static void menu_pomodoro_play_pause(void) {
    timer_is_running = !timer_is_running;
}

static void menu_pomodoro_start_10(void) {
    timer_start = timer_read32();
    timer_max = 10 * 60 * 1000;
    timer_is_running = true;
}

static void load_screen_pomodoro_base(void) {
    load_screen_xx_base(menus, sizeof(menus) / sizeof(obj_update_dilemma_menu_t), ui_screen_pomodoro);
}

static void load_screen_pomodoro_menu(void) {
    load_screen_xx_menu(menus, sizeof(menus) / sizeof(obj_update_dilemma_menu_t), ui_screen_pomodoro_menu);
}

void housekeeping_task_screen_pomodoro(void) {
    static int last_layer;
    int current_layer = get_highest_layer(layer_state);

    if (current_layer != last_layer)
    {
        switch (current_layer)
        {
            case 0:
                if (screen_index == 1) {
                    load_screen_pomodoro_base();
                    trigger_menu_element(menus, menu_index);
                    menu_index = 0;
                    screen_index = 0;
                }
                break;
            case LAYER_MENU:
                if (screen_index == 0) {
                    load_screen_pomodoro_menu();
                    screen_index = 1;
                }
                break;
        }
    }

    for (int i = 0; i < sizeof(widgets) / sizeof(obj_update_dilemma_pomodoro_status_t); i++)
    {
        lv_obj_t* obj = widgets[i].obj;
        if (obj && widgets[i].update_function){
            widgets[i].update_function(obj);
        }
    }

    last_layer = current_layer;
}

static void update_pomodoro_time(lv_obj_t* obj) {
    if (timer_is_running == true) {
        uint32_t elapsed = timer_max - timer_elapsed32(timer_start);
        if (timer_max < timer_elapsed32(timer_start)) {
            elapsed = 0;
        }
        uint16_t minutes_elapsed = elapsed / 60000;
        uint16_t seconds_elapsed = (elapsed % 60000) / 1000;

        char buffer[50];
        char* at = buffer;
        at += sprintf(at, "%02u", minutes_elapsed);
        at += sprintf(at, ":");
        at += sprintf(at, "%02u", seconds_elapsed);

        lv_label_set_text(obj, buffer);
    }
    // else {
    //     lv_label_set_text(obj, "00:00");
    // }
}

static lv_obj_t* ui_create_pomodoro_title(lv_obj_t* cont) {
    lv_obj_t* button = lv_btn_create(cont);
    ui_styles_t* styles = get_current_ui_styles();
    lv_obj_add_style(button, &styles->layer_name, 0);
    lv_obj_set_flex_grow(button, 1); // take all remaining space in line

    lv_obj_t* label = lv_label_create(button);
    // TODO, this should not be here
    lv_label_set_text(label, "POMODORO");
    lv_obj_center(label);
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    lv_obj_set_height(label, 30);
    // lv_obj_set_x(ui_label_layer, 0);
    // lv_obj_set_y(ui_label_layer, 20);

    return label;
}

// for now only a text timer with dummy content
// TODO change text
static lv_obj_t* ui_create_pomodoro_time(lv_obj_t* cont) {
    lv_obj_t* button = lv_btn_create(cont);
    ui_styles_t* styles = get_current_ui_styles();


    static lv_style_t style_text;
    lv_style_init(&style_text);
    // memcpy(&style_text, &styles->layer_name, sizeof(lv_style_t));
    lv_style_set_text_font(&style_text, &jetbrainsmonoextrabold60);
    lv_style_set_text_color(&style_text, lv_color_make(255, 255, 255));
    lv_style_set_text_opa(&style_text, 255);

    lv_obj_add_style(button, &styles->layer_name, 0);
    lv_obj_add_style(button, &style_text, 0);
    lv_obj_set_flex_grow(button, 1);                        // take all remaining space in line
    lv_obj_add_flag(button, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK); // new line

    lv_obj_t* label = lv_label_create(button);
    // TODO, should this really be here?
    lv_label_set_text(label, "00:00");
    lv_obj_center(label);
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    // lv_obj_set_height(label, 80);

    return label;
}

// static lv_obj_t* ui_create_pomodoro_arc(lv_obj_t* cont) {
//     lv_obj_t* button = lv_btn_create(cont);
//     lv_obj_t* arc = lv_arc_create(button);
//     ui_styles_t* styles = get_current_ui_styles();

//     lv_obj_add_flag(button, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK); // new line
//     lv_obj_set_flex_grow(button, 1);                        // take all remaining space in line
//     lv_obj_add_style(button, &styles->layer_name, 0);

//     lv_obj_add_style(arc, &styles->bar, LV_PART_INDICATOR);

//     // TODO move this into a style init
//     // we could do a post-processing in style init where we copy over the
//     // values... right now this breaks on theme change each screen should have
//     // their own load_themes function, right now everything lives in theme.c
//     // ideally we should derive a new pomodoro_styles->arc from styles->bar, and
//     // then set the arc specific styles in the init function copy the bar color
//     // into the arc color
//     lv_style_value_t v;
//     lv_res_t res = lv_style_get_prop(&styles->bar, LV_STYLE_BG_COLOR, &v);
//     if (res == LV_RES_OK)
//     { /*Found*/
//         lv_style_set_arc_color(&styles->bar, v.color);
//         // hide the knob
//         lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
//     }

//     lv_obj_set_size(arc, 50, 50);
//     lv_arc_set_rotation(arc, 135);
//     lv_arc_set_bg_angles(arc, 0, 270);
//     lv_arc_set_value(arc, 100);
//     lv_obj_center(arc);

//     return arc;
// }

// static void update_pomodoro_bar(lv_obj_t* obj) {
//     if (timer_is_running == true) {
//         uint32_t elapsed = timer_max - timer_elapsed32(timer_start);
//         uint16_t elapsed_percent = (elapsed * 100) / timer_max;
//         if (elapsed_percent < 0) {
//             elapsed_percent = 0;
//         }
//         lv_bar_set_value(obj, elapsed_percent, LV_ANIM_OFF);
//     }
// }

// TODO the layer is hardcoded.... and it will be different on Dilemma and Dilemma
bool process_record_screen_pomodoro(uint16_t keycode, keyrecord_t* record)
{
    if (screen_index == 1) {
        process_record_menu(keycode, record, menus, &menu_index, sizeof(menus) / sizeof(obj_update_dilemma_menu_t));
    }

    return true;
}
