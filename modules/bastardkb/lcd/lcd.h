#ifndef LCD_H
#define LCD_H

#include QMK_KEYBOARD_H
#include "introspection.h"
#include "lvgl.h"
#include <ctype.h>
#include "theme.h"

enum modules {
    MODULE_BASE = 0,
    MODULE_POMODORO,
};

typedef struct {
    void (*load_module)(void);
    void (*init_module)(void);
    void (*load_custom_theme_elements)(void);
    void (*update_custom_elements_styles_from_current_theme)(void);
    bool (*process_record)(uint16_t keycode, keyrecord_t* record);
    void (*housekeeping_task)(void);
} lcd_module_t;

void keyboard_post_init_lcd(void);

void housekeeping_task_lcd(void);
bool process_record_lcd(uint16_t keycode, keyrecord_t* record);

void keycode_sync_handler(uint8_t initiator2target_buffer_size, const void* initiator2target_buffer, uint8_t target2initiator_buffer_size, void* target2initiator_buffer);
void refresh_lcd_info(void);
void cycle_theme_and_save_in_eeprom(void);
void init_display(void);
void write_config_to_eeprom(void);

void set_current_module(const uint8_t module);
void module_sync_handler(uint8_t initiator2target_buffer_size, const void* initiator2target_buffer, uint8_t target2initiator_buffer_size, void* target2initiator_buffer);

#endif
