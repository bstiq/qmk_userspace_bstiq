#include QMK_KEYBOARD_H
#include "introspection.h"
#include "lvgl.h"
#include <ctype.h>

#include "config.h"
#include "screen_base.h"
#include "screen_pomodoro.h"
#include "lcd.h"
#include "menu.h"
#include "ui_elements.h"
#include "transactions.h"
#include "dilemma_sync.h"

typedef struct {
    lv_obj_t* obj;
    void (*update_function)(lv_obj_t*, dilemma_status_t current_status, dilemma_status_t prev_status);
} obj_update_dilemma_lcd_status_t;

dilemma_status_t dilemma_lcd_status_prev = { 0 };
dilemma_status_t dilemma_lcd_status = { 0 };

static void update_layer_name(lv_obj_t* obj, const dilemma_status_t current_status, const dilemma_status_t prev_status);
static void update_rgb_value(lv_obj_t* obj, const dilemma_status_t current_status, const dilemma_status_t prev_status);
static void update_rgb_bar(lv_obj_t* obj, const dilemma_status_t current_status, const dilemma_status_t prev_status);
static void update_mod_shift(lv_obj_t* obj, const dilemma_status_t current_status, const dilemma_status_t prev_status);
static void update_mod_ctrl(lv_obj_t* obj, const dilemma_status_t current_status, const dilemma_status_t prev_status);
static void update_mod_alt(lv_obj_t* obj, const dilemma_status_t current_status, const dilemma_status_t prev_status);
static void update_mod_gui(lv_obj_t* obj, const dilemma_status_t current_status, const dilemma_status_t prev_status);
static void update_mod_snipe(lv_obj_t* obj, const dilemma_status_t current_status, const dilemma_status_t prev_status);
static void update_mod_scroll(lv_obj_t* obj, const dilemma_status_t current_status, const dilemma_status_t prev_status);
static void update_mod_snipe_dpi_bar(lv_obj_t* obj, const dilemma_status_t current_status, const dilemma_status_t prev_status);
static void update_mod_snipe_dpi_number(lv_obj_t* obj, const dilemma_status_t current_status, const dilemma_status_t prev_status);
static void update_mod_dpi_bar(lv_obj_t* obj, const dilemma_status_t current_status, const dilemma_status_t prev_status);
static void update_mod_dpi_number(lv_obj_t* obj, const dilemma_status_t current_status, const dilemma_status_t prev_status);
static void update_mod_xx(lv_obj_t* obj, uint8_t mod_mask, const dilemma_status_t current_status, const dilemma_status_t prev_status);
static void update_rgb_effect(lv_obj_t* obj, const dilemma_status_t current_status, const dilemma_status_t prev_status);
static const char* rgb_matrix_get_effect_name(void);
static void menu_base_go_pomodoro(void);
static void menu_base_change_theme(void);
static void load_screen_base_menu(void);
static void load_screen_base_base(void);
static void refresh_screen_base(void);

static lv_obj_t* ui_screen_base;
static lv_obj_t* ui_screen_base_menu;

static obj_update_dilemma_lcd_status_t widgets[14];
static obj_update_dilemma_menu_t menus[3];
static uint8_t screen_index = 0;
static uint8_t menu_index = 0;

void load_module_base(void) {
    load_screen_base_base();
}

void init_screen_base(void) {
    ui_screen_base = lv_obj_create(NULL);
    lv_obj_t* cont = ui_create_container(ui_screen_base);

    ui_screen_base_menu = lv_obj_create(NULL);
    lv_obj_t* cont_menu = ui_create_container(ui_screen_base_menu);

    /* ----- Widgets ----- */
    widgets[0] = (obj_update_dilemma_lcd_status_t){ ui_create_layer_label(cont), &update_layer_name, };
    widgets[1] = (obj_update_dilemma_lcd_status_t){ ui_create_mod_button(cont, "SHFT", true, MOD_MASK_SHIFT), &update_mod_shift, };
    widgets[2] = (obj_update_dilemma_lcd_status_t){ ui_create_mod_button(cont, "ALT", false, MOD_MASK_ALT), &update_mod_alt, };
    widgets[3] = (obj_update_dilemma_lcd_status_t){ ui_create_mod_button(cont, "CTRL", false, MOD_MASK_CTRL), &update_mod_ctrl, };
    widgets[4] = (obj_update_dilemma_lcd_status_t){ ui_create_mod_button(cont, "GUI", false, MOD_MASK_GUI), &update_mod_gui, };
    ui_create_line_separator(cont, 1, 3);
    widgets[5] = (obj_update_dilemma_lcd_status_t){ ui_create_mod_button(cont, "SCROLL", true, 0), &update_mod_scroll, };
    widgets[6] = (obj_update_dilemma_lcd_status_t){ ui_create_mod_button(cont, "SNIPE", false, 0), &update_mod_snipe, };

    // sniping DPI widgets
    ui_create_secondary_text(cont, "SNIPE DPI", true, 4);
    widgets[7] = (obj_update_dilemma_lcd_status_t){ ui_create_progress_bar(cont, 4), &update_mod_snipe_dpi_bar, };
    widgets[8] = (obj_update_dilemma_lcd_status_t){ ui_create_number_label(cont, 2), &update_mod_snipe_dpi_number, };

    // regular DPI widgets
    ui_create_secondary_text(cont, "DPI", true, 2);
    widgets[9] = (obj_update_dilemma_lcd_status_t){ ui_create_progress_bar(cont, 6), &update_mod_dpi_bar, };
    widgets[10] = (obj_update_dilemma_lcd_status_t){ ui_create_number_label(cont, 2), &update_mod_dpi_number, };

    // line separator
    ui_create_line_separator(cont, 1, 3);

    // rgb widgets
    ui_create_secondary_text(cont, "RGB", true, 2);
    widgets[11] = (obj_update_dilemma_lcd_status_t){ ui_create_progress_bar(cont, 6), &update_rgb_bar, };
    widgets[12] = (obj_update_dilemma_lcd_status_t){ ui_create_number_label(cont, 2), &update_rgb_value, };
    widgets[13] = (obj_update_dilemma_lcd_status_t){ ui_create_secondary_text(cont, "effect...", true, 1), &update_rgb_effect };

    /* ----- menus ----- */
    menus[0] = (obj_update_dilemma_menu_t){
        ui_create_menu_line(cont_menu, "Back to main"),
        NULL,
    };
    menus[1] = (obj_update_dilemma_menu_t){
        ui_create_menu_line(cont_menu, "Pomodoro"),
        &menu_base_go_pomodoro,
    };
    // TODO later, create a "theme options" module? but then at each bootmagic flash it will be erased :(
    menus[2] = (obj_update_dilemma_menu_t){
        ui_create_menu_line(cont_menu, "Change theme"),
        &menu_base_change_theme,
    };
}

void housekeeping_task_screen_base(void) {
    dilemma_lcd_status = get_dilemma_status();
    refresh_screen_base();
    dilemma_lcd_status_prev = dilemma_lcd_status;
}


static void menu_base_go_pomodoro(void) {
    set_current_module(MODULE_POMODORO);
}

static void menu_base_change_theme(void) {
    cycle_theme_and_save_in_eeprom();
    // TODO remove this? should only be called by left anyway
    // if (is_keyboard_left()) {
        update_styles_from_current_theme();
    // }
}

static const char* rgb_matrix_get_effect_name(void) {
    // thank you drashna!
    static char    buf[32] = { 0 };
    static uint8_t last_effect = 0;
    if (last_effect != rgb_matrix_get_mode()) {
        last_effect = rgb_matrix_get_mode();
        snprintf(buf, sizeof(buf), "%s", rgb_matrix_get_mode_name(rgb_matrix_get_mode()));
        for (uint8_t i = 1; i < sizeof(buf); ++i) {
            if (buf[i] == 0)
                break;
            else if (buf[i] == '_')
                buf[i] = ' ';
            else if (buf[i - 1] == ' ')
                buf[i] = toupper(buf[i]);
            else if (buf[i - 1] != ' ')
                buf[i] = tolower(buf[i]);
        }
    }
    return buf;
}

// TODO add dilemma layers, not only MAX
static void update_layer_name(lv_obj_t* obj, const dilemma_status_t current_status, const dilemma_status_t prev_status) {
    if (current_status.layer != prev_status.layer) {
        switch (current_status.layer) {
            case 0:
            default:
                lv_label_set_text(obj, "LAYER: BASE");
                break;
            case 1:
                lv_label_set_text(obj, "LAYER: LOWER");
                break;
            case 2:
                lv_label_set_text(obj, "LAYER: RAISE");
                break;
            case 3:
                lv_label_set_text(obj, "LAYER: MOUSE");
                break;
        }
    }
}

static void update_mod_xx(lv_obj_t* obj, uint8_t mod_mask, const dilemma_status_t current_status, const dilemma_status_t prev_status) {
    if ((current_status.mods & mod_mask) != (prev_status.mods & mod_mask)) {
        if ((current_status.mods & mod_mask)) {
            lv_event_send(obj, LV_EVENT_PRESSED, NULL);
        }
        else {
            lv_event_send(obj, LV_EVENT_RELEASED, NULL);
        }
    }
}

static void update_mod_shift(lv_obj_t* obj, const dilemma_status_t current_status, const dilemma_status_t prev_status) {
    update_mod_xx(obj, MOD_MASK_SHIFT, current_status, prev_status);
}
static void update_mod_ctrl(lv_obj_t* obj, const dilemma_status_t current_status, const dilemma_status_t prev_status) {
    update_mod_xx(obj, MOD_MASK_CTRL, current_status, prev_status);
}
static void update_mod_alt(lv_obj_t* obj, const dilemma_status_t current_status, const dilemma_status_t prev_status) {
    update_mod_xx(obj, MOD_MASK_ALT, current_status, prev_status);
}
static void update_mod_gui(lv_obj_t* obj, const dilemma_status_t current_status, const dilemma_status_t prev_status) {
    update_mod_xx(obj, MOD_MASK_GUI, current_status, prev_status);
}

static void update_rgb_effect(lv_obj_t* obj, const dilemma_status_t current_status, const dilemma_status_t prev_status) {
    const bool rgb_change = (current_status.rgb_enabled != prev_status.rgb_enabled);

    if (!current_status.rgb_enabled) {
        if (rgb_change) {
            lv_label_set_text(obj, "");
        }
    }
    else {
        if ((rgb_change) || (current_status.rgb_effect_mode != prev_status.rgb_effect_mode)) {
            const char* effect_name = rgb_matrix_get_effect_name();
            lv_label_set_text(obj, effect_name);
        }
    }
}

static void update_rgb_value(lv_obj_t* obj, const dilemma_status_t current_status, const dilemma_status_t prev_status) {
    const bool rgb_change = (current_status.rgb_enabled != prev_status.rgb_enabled);

    if (!current_status.rgb_enabled) {
        if (rgb_change) {
            lv_label_set_text(obj, "Off");
        }
    }
    else {
        if ((rgb_change) || (current_status.rgb_val != prev_status.rgb_val)) {
            char rgbval[50];
            sprintf(rgbval, "%u", current_status.rgb_val);
            lv_label_set_text(obj, rgbval);
        }
    }
}

static void update_rgb_bar(lv_obj_t* obj, const dilemma_status_t current_status, const dilemma_status_t prev_status) {
    const bool rgb_change = (current_status.rgb_enabled != prev_status.rgb_enabled);
    if (!current_status.rgb_enabled) {
        if (rgb_change) {
            lv_bar_set_value(obj, 0, LV_ANIM_OFF);
        }
    }
    else {
        if ((rgb_change) || (current_status.rgb_val != prev_status.rgb_val)) {
            float rel = (float)(current_status.rgb_val) * 100 / 156;
            lv_bar_set_value(obj, (uint16_t)rel, LV_ANIM_OFF);
        }
    }
}

static void update_mod_scroll(lv_obj_t* obj, const dilemma_status_t current_status, const dilemma_status_t prev_status) {
    if (current_status.scrolling != prev_status.scrolling) {
        uint32_t event = current_status.scrolling ? LV_EVENT_PRESSED : LV_EVENT_RELEASED;
        lv_event_send(obj, event, NULL);
    }
}

static void update_mod_snipe(lv_obj_t* obj, const dilemma_status_t current_status, const dilemma_status_t prev_status) {
    if (current_status.sniping != prev_status.sniping) {
        uint32_t event = current_status.sniping ? LV_EVENT_PRESSED : LV_EVENT_RELEASED;
        lv_event_send(obj, event, NULL);
    }
}

// TODO dynamically get max DPI, instead of using hardcoded values
static void update_mod_snipe_dpi_number(lv_obj_t* obj, const dilemma_status_t current_status, const dilemma_status_t prev_status) {
    if (current_status.s_dpi != prev_status.s_dpi) {
        char c_s_dpi[50];
        sprintf(c_s_dpi, "%u", (uint16_t)current_status.s_dpi);
        lv_label_set_text(obj, c_s_dpi);
    }
}

// TODO dynamically get max DPI, instead of using hardcoded values
static void update_mod_snipe_dpi_bar(lv_obj_t* obj, const dilemma_status_t current_status, const dilemma_status_t prev_status) {
    if (current_status.s_dpi != prev_status.s_dpi) {
        static const uint16_t rel_max_s_dpi = 100 * 4;
        const float           rel = (float)((current_status.s_dpi + 100 - 200)) * 100 / rel_max_s_dpi;
        lv_bar_set_value(obj, (uint16_t)rel, LV_ANIM_OFF);
    }
}

// TODO dynamically get max DPI, instead of using hardcoded values
static void update_mod_dpi_number(lv_obj_t* obj, const dilemma_status_t current_status, const dilemma_status_t prev_status) {
    if (current_status.dpi != prev_status.dpi) {
        char c_dpi[50];
        sprintf(c_dpi, "%u", (uint16_t)current_status.dpi);
        lv_label_set_text(obj, c_dpi);
    }
}

// TODO dynamically get max DPI, instead of using hardcoded values
static void update_mod_dpi_bar(lv_obj_t* obj, const dilemma_status_t current_status, const dilemma_status_t prev_status) {
    if (current_status.dpi != prev_status.dpi) {
        static const uint16_t rel_max_dpi = 200 * 16;
        const float           rel = (float)((current_status.dpi + 200 - 400)) * 100 / rel_max_dpi;
        lv_bar_set_value(obj, (uint16_t)rel, LV_ANIM_OFF);
    }
}

static void load_screen_base_base(void) {
    load_screen_xx_base(menus, sizeof(menus) / sizeof(obj_update_dilemma_menu_t), ui_screen_base);
}

static void load_screen_base_menu(void) {
    load_screen_xx_menu(menus, sizeof(menus) / sizeof(obj_update_dilemma_menu_t), ui_screen_base_menu);
}

void refresh_screen_base(void) {
    const dilemma_status_t current_status = (const dilemma_status_t)dilemma_lcd_status;
    const dilemma_status_t prev_status = (const dilemma_status_t)dilemma_lcd_status_prev;
    static int last_layer;
    int        current_layer = get_highest_layer(layer_state);

    if (current_layer != last_layer) {
        switch (current_layer) {
            case 0:
                // default:
                    // we only trigger things if we come back from the menu layer
                if (screen_index == 1) {
                    load_screen_base_base();
                    trigger_menu_element(menus, menu_index);
                    menu_index = 0;
                    screen_index = 0;
                }
                break;
            case LAYER_MENU:
                if (screen_index == 0) {
                    load_screen_base_menu();
                    screen_index = 1;
                }
                break;
            default:
                break;
        }
    }

    if (is_keyboard_left()) {
        for (int i = 0; i < sizeof(widgets) / sizeof(obj_update_dilemma_lcd_status_t); i++) {
            lv_obj_t* obj = widgets[i].obj;
            if (obj && widgets[i].update_function)
                widgets[i].update_function(obj, current_status, prev_status);
        }
    }

    last_layer = current_layer;
}

bool process_record_screen_base(uint16_t keycode, keyrecord_t* record) {
    // TODO index is hardcoded...
    if (screen_index == 1) {
        process_record_menu(keycode, record, menus, &menu_index, sizeof(menus) / sizeof(obj_update_dilemma_menu_t));
    }
    return true;
}
