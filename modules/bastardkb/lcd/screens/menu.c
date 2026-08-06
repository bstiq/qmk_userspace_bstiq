#include "menu.h"
#include "theme.h"

void press_menu_button(const obj_update_dilemma_menu_t button){
    if(button.obj != NULL){
        lv_event_send(button.obj, LV_EVENT_PRESSED, NULL);
    }
}

void release_menu_button(const obj_update_dilemma_menu_t button){
    if(button.obj != NULL){
        lv_event_send(button.obj, LV_EVENT_RELEASED, NULL);
    }
}

bool process_record_menu(uint16_t keycode, keyrecord_t *record, const obj_update_dilemma_menu_t menus[], uint8_t *menu_index, const uint8_t menu_size){
    switch (keycode) {
        case LCD_MENU_NEXT:
            if (record->event.pressed) {
                release_all_buttons(menus, menu_size);
                *menu_index = (*menu_index + 1) % menu_size;
                press_menu_button(menus[*menu_index]);
            }
            break;
        case LCD_MENU_PREV:
            if (record->event.pressed) {
                release_all_buttons(menus, menu_size);
                *menu_index = (*menu_index - 1 + menu_size) % menu_size;
                press_menu_button(menus[*menu_index]);
            }
            break;
    }
    return true;
}

void release_all_buttons(const obj_update_dilemma_menu_t menus[], const uint8_t menu_size){
    for (int i = 0; i < menu_size; i++) {
        release_menu_button(menus[i]);
    }
}

lv_obj_t *ui_create_menu_line(lv_obj_t *cont, const char *text) {
    lv_obj_t *button   = lv_btn_create(cont);
    ui_styles_t *styles = get_current_ui_styles();
    lv_obj_add_style(button, &styles->mod_btn, 0);
    lv_obj_add_style(button, &styles->mod_btn_pressed, LV_STATE_PRESSED);
    // lv_obj_set_height(button, 33);
    lv_obj_set_flex_grow(button, 1);

    lv_obj_add_flag(button, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);

    return button;
}

void trigger_menu_element(const obj_update_dilemma_menu_t menus[], uint8_t menu_index){
    if(menus[menu_index].update_function != NULL){
        menus[menu_index].update_function();
    }
}