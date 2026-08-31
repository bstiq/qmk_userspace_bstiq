// hacks to get introspection working with Argos' dynamic features

#include QMK_KEYBOARD_H

combo_t key_combos[ARGOS_COMBO_ENTRIES];
tap_dance_action_t tap_dance_actions[ARGOS_TAP_DANCE_ENTRIES];

// leave space for a LOT of pointing device codes, 
// to make mode management easier in the pointing device module 
// and give ourselves some breathing room for future features
// so we start at 0x7E60

// momentary switch layer exclusive - 32 layers max
#define BK_MOMENTARY_EXCLUSIVE 0x7E60 
#define BK_MOMENTARY_EXCLUSIVE_MAX 0x7E7F
#define MOE(layer) (BK_MOMENTARY_EXCLUSIVE | ((layer) & 0x1F))
#define BK_MOMENTARY_EXCLUSIVE_GET_LAYER(kc) (QK_MOMENTARY_GET_LAYER(kc))

// L-ayer M-od E-xclusive: Momentary switch layer with modifiers active - 16 layer max
#define BK_LAYER_MOD_EXCLUSIVE 0x7E80
#define BK_LAYER_MOD_EXCLUSIVE_MAX 0x7E9F
#define LME(layer, mod) (BK_LAYER_MOD_EXCLUSIVE | (((layer) & 0xF) << 5) | ((mod) & 0x1F))
#define BK_LAYER_MOD_EXCLUSIVE_GET_LAYER(kc) (QK_LAYER_MOD_GET_LAYER(kc))
#define BK_LAYER_MOD_EXCLUSIVE_GET_MODS(kc) (QK_LAYER_MOD_GET_MODS(kc))

// momentary switch layer exclusive when held, key on tap
// TODO

// momentary switch layer exclusive with modifiers active when held, key on tap
// TODO