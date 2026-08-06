#include QMK_KEYBOARD_H
#include "introspection.h"

#include "lcd.h"
#include "config.h"
#include "ui_elements.h"
#include "quantum.h"
#include <stdio.h>

#include "qp.h"
#include "qp_comms.h"
#include "qp_st77xx_opcodes.h"
#include "color.h"
#include "transactions.h"

// TODO manage this better
#include "screens/screen_base.h"
#include "screens/screen_pomodoro.h"

painter_device_t lcd;

typedef struct {
    uint16_t keycode;
    keyrecord_t record;
} dilemma_keycode_event_t;

typedef struct {
    uint8_t module_id;
} dilemma_module_event_t;

lcd_module_t lcd_module_base = {
    .init_module = &init_screen_base,
    .load_custom_theme_elements = &load_themes,
    .load_module = &load_module_base,
    .update_custom_elements_styles_from_current_theme = &update_styles_from_current_theme,
    .process_record = &process_record_screen_base,
    .housekeeping_task = &housekeeping_task_screen_base,
};

lcd_module_t lcd_module_pomodoro = {
    .init_module = &init_screen_pomodoro,
    .load_custom_theme_elements = NULL,
    .load_module = &load_module_pomodoro,
    .update_custom_elements_styles_from_current_theme = NULL,
    .process_record = &process_record_screen_pomodoro,
    .housekeeping_task = &housekeeping_task_screen_pomodoro,
};

lcd_module_t* lcd_modules[] = { &lcd_module_base, &lcd_module_pomodoro };

static uint8_t selected_module = MODULE_BASE;

void init_display(void) {
    // Display timeout
    wait_ms(LCD_WAIT_TIME);

    lcd = qp_st7789_make_spi_device(LCD_WIDTH, LCD_HEIGHT, LCD_CS_PIN, LCD_DC_PIN, LCD_RST_PIN, LCD_SPI_DIVISOR, SPI_MODE);
    qp_init(lcd, LCD_ROTATION);

    // Display offset
    qp_set_viewport_offsets(lcd, LCD_OFFSET_X, LCD_OFFSET_Y);

    qp_lvgl_attach(lcd);

    // Power on display, fill with black
    qp_power(lcd, 1);
    qp_rect(lcd, 0, 0, 300, 300, HSV_BLACK, 1);
    qp_flush(lcd);

    // TODO move the load themes and init styles into the custom structure
    load_themes();
    init_styles();

    for (int i = 0; i < sizeof(lcd_modules) / sizeof(lcd_module_t*); i++) {
        if (lcd_modules[i]->init_module != NULL) {
            lcd_modules[i]->init_module();
        }

        lcd_modules[selected_module]->load_module();
    }
}

void set_current_module(const uint8_t module) {
    if (module >= sizeof(lcd_modules) / sizeof(lcd_module_t*)) {
        selected_module = MODULE_BASE;
    }
    else
        selected_module = module;

    if (lcd_modules[selected_module]->load_module) {
        lcd_modules[selected_module]->load_module();
    }
}

void keyboard_post_init_lcd(void) {
    load_dilemma_theme_config_from_eeprom();
    if (is_keyboard_left()) {
        init_display();
    }

    transaction_register_rpc(RPC_ID_KEYCODE_SYNC, keycode_sync_handler);
}

void housekeeping_task_lcd(void) {
    if (is_keyboard_left()) {
        if (lcd_modules[selected_module]->housekeeping_task != NULL) {
            lcd_modules[selected_module]->housekeeping_task();
        }
    }
}

// if right side is primary, we want to send the keycodes to the left for LCD processing
// TODO retry if fails?
bool process_record_lcd(uint16_t keycode, keyrecord_t* record) {
    if (is_keyboard_master()) {
        if (is_keyboard_left()) {
            if (lcd_modules[selected_module]->process_record != NULL) {
                lcd_modules[selected_module]->process_record(keycode, record);
            }
        }
        else {
            // if keyboard right is primary, send the keycode event to the left for processing
            dilemma_keycode_event_t dilemma_keycode_event = {
                            .keycode = keycode,
                            .record = *record,
            };
            transaction_rpc_send(RPC_ID_KEYCODE_SYNC, sizeof(dilemma_keycode_event), &dilemma_keycode_event);
        }
    }
    return true;
}


void keycode_sync_handler(uint8_t initiator2target_buffer_size, const void* initiator2target_buffer, uint8_t target2initiator_buffer_size, void* target2initiator_buffer) {
    // it only makes sense to process this if we are on the left (with the screen)
    if (is_keyboard_left()) {
        if (initiator2target_buffer_size == sizeof(dilemma_keycode_event_t)) {
            dilemma_keycode_event_t dilemma_keycode_event = *(const dilemma_keycode_event_t*)initiator2target_buffer;
            if (lcd_modules[selected_module]->process_record != NULL) {
                lcd_modules[selected_module]->process_record(dilemma_keycode_event.keycode, &dilemma_keycode_event.record);
            }
        }
    }
}
