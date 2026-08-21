#pragma once

#ifdef POINTING_DEVICE_DRIVER_digitizer
#include "digitizer.h"
#endif

typedef union {
    uint8_t raw;
    struct {
        bool invert_x : 1;
        bool invert_y : 1;
        uint8_t current_dpi_step; // up to 2^8 = 256 steps
        uint8_t dpi_per_step; // up to 256 config step at a time. should be more than enough
        uint8_t max_dpi_steps: 8; // we can optionally limit the max DPI to a certain amount of bytes
        uint16_t minimum_dpi;
    } __attribute__((packed));
} bkpd_mode_t;

typedef union {
    uint8_t raw;
    struct {
        // uint8_t pointer_default_dpi : BK_POINTING_DEVICE_MAX_DPI_BYTES;         // 16 steps available.
        // uint8_t pointer_sniping_dpi : BK_POINTING_DEVICE_MAX_SNIPING_DPI_BYTES; // 4 steps available.
        // bool    is_dragscroll_enabled : 1;
        // bool    is_sniping_enabled : 1;
        bool    auto_mouse_layer_enabled : 1;
        bool    auto_precision_on_mouse_layer_enabled : 1;
        // bool    dragscroll_axis_invert_x : 1;
        // bool    dragscroll_axis_invert_y : 1;
        bool    has_copied_qmk_config : 1;
        // bool    is_cursor_enabled : 1;
        bkpd_mode_t modes_config[4]; // !!! TODO CHANGE THIS WHEN WE ADD MORE MODES
        uint8_t active_mode;
        // TODO add invert/DPI for all modes
        // TODO for dpi: init at #define value
    } __attribute__((packed));
} bkpd_config_t;

// TODO get rid of unused functions here
bool bkpd_dispatch_command(const uint8_t command_id, uint8_t **command_data);
void bkpd_build_pointing_device_info_command_data(uint8_t **command_data);
uint16_t bkpd_get_pointer_default_dpi(void);
void bkpd_set_pointer_default_dpi(uint16_t new_dpi);
void bkpd_cycle_pointer_default_dpi(bool forward);
void bkpd_cycle_pointer_default_dpi_noeeprom(bool forward);
void bkpd_cycle_pointer_sniping_dpi(bool forward);
void bkpd_cycle_pointer_sniping_dpi_noeeprom(bool forward);
bool bkpd_get_pointer_sniping_enabled(void);
void bkpd_set_pointer_sniping_enabled(bool enable);
bool bkpd_get_pointer_dragscroll_enabled(void);
void bkpd_set_pointer_dragscroll_enabled(bool enable);
uint16_t bkpd_get_pointer_sniping_dpi(void);
void bkpd_set_pointer_sniping_dpi(uint16_t new_dpi);
void keyboard_post_init_bk_pointing_device(void);
void bkpd_set_auto_mouse_layer_enabled(bool enabled);
void bkpd_set_auto_precision_on_mouse_layer_enabled(bool enabled);
bool bkpd_get_auto_mouse_layer_enabled(void);
bool bkpd_get_auto_precision_on_mouse_layer_enabled(void);
void bkpd_set_dragscroll_axis_invert_x(bool invert);
void bkpd_set_dragscroll_axis_invert_y(bool invert);
void bkpd_set_dragscroll_dpi(uint16_t dpi);
bool bkpd_get_dragscroll_axis_invert_x(void);
bool bkpd_get_dragscroll_axis_invert_y(void);
uint16_t bkpd_get_dragscroll_dpi(void);
uint16_t bkpd_get_minimum_default_dpi(void);
uint16_t bkpd_get_default_dpi_config_step(void);
uint16_t bkpd_get_minimum_sniping_dpi(void);
uint16_t bkpd_get_sniping_dpi_config_step(void);
void bkpd_set_pointer_cursor_enabled(bool enable);
void write_bkpd_config_to_eeprom(void);
bool bkpd_is_changing_dpi_settings(void);

#ifdef POINTING_DEVICE_DRIVER_digitizer
bool digitizer_task_kb(digitizer_t *const digitizer_state);
#endif

// NOTE: made to work on branch bkb-pointing-device
