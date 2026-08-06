#include "utilities.h"

void load_screen_xx_base(obj_update_dilemma_menu_t menus[], uint8_t menu_size, lv_obj_t* ui_screen) {
    release_all_buttons(menus, menu_size);
    lv_disp_load_scr(ui_screen);
}

void load_screen_xx_menu(obj_update_dilemma_menu_t menus[], uint8_t menu_size, lv_obj_t* ui_screen) {
    release_all_buttons(menus, menu_size);
    press_menu_button(menus[0]);
    lv_disp_load_scr(ui_screen);
}
