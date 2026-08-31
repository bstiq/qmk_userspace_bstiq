// hacks to get introspection working with Argos' dynamic features

#include QMK_KEYBOARD_H

combo_t key_combos[ARGOS_COMBO_ENTRIES];
tap_dance_action_t tap_dance_actions[ARGOS_TAP_DANCE_ENTRIES];

// leave space for a LOT of pointing device codes, 
// to make mode management easier in the pointing device module

// RANGE NOT USED - FOR LO capability
#define LO_0 0x7E60 // sync with BK_LO
#define LO_LAST 0x7E6A // not used, only for delimitation

// RANGE NOT USED - FOR LTO capability
#define LTO_0 0x7E6B // sync with BK_LTO
#define LTO_LAST 0x7E75 // not used, only for delimitation

// Custom layer keys
#define BK_LO 0x7E60 // sync with ALO_0
#define BK_LTO 0x7E6B // sync with ALTO_0

// momentary switch layer, disable all other layers
#define LO(layer) (BK_LO | ((layer) & 0x09)) // 10 layers max (0-9)

// momentary switch layer on hold, disable all other layers
#define LTO(layer, kc) (BK_LTO | (((layer) & 0xF) << 8) | ((kc) & 0xFF))