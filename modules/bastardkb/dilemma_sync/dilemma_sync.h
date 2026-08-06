#include QMK_KEYBOARD_H

typedef struct {
    uint8_t                mods;
    bool                   sniping;
    bool                   scrolling;
    uint8_t                layer;
    uint8_t                current_theme_id;
    uint8_t                rgb_enabled;
    uint8_t                rgb_effect_mode;
    uint16_t               rgb_val;
    uint16_t               dpi;
    uint16_t               s_dpi;
} dilemma_status_t;

const dilemma_status_t get_dilemma_status(void);