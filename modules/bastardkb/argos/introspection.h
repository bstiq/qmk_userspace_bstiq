// hacks to get introspection working with Argos' dynamic features

#include QMK_KEYBOARD_H

combo_t key_combos[ARGOS_COMBO_ENTRIES];
tap_dance_action_t tap_dance_actions[ARGOS_TAP_DANCE_ENTRIES];

// leave space for a LOT of pointing device codes, 
// to make mode management easier in the pointing device module
#define ALO_0 0x7E60 
// leave space for 10 layers
#define ALO_1 0x7E61
#define ALO_2 0x7E62
#define ALO_3 0x7E63
#define ALO_4 0x7E64
#define ALO_5 0x7E65
#define ALO_6 0x7E66
#define ALO_7 0x7E67
#define ALO_8 0x7E68
#define ALO_9 0x7E69
#define ALO_LAST 0x7E6A // not used, only for delimitation