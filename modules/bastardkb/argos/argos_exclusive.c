/*
 * Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Publicw License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include QMK_KEYBOARD_H
#include "argos_exclusive.h"

extern bool hold_layer;
extern layer_state_t LO_layer_state;

action_t bk_action_for_keycode(uint16_t keycode){
    action_t action = {0};
    uint8_t action_layer, mod;
    switch(keycode){
        case BK_MOMENTARY_EXCLUSIVE ... BK_MOMENTARY_EXCLUSIVE_MAX: {
            action_layer = BK_MOMENTARY_EXCLUSIVE_GET_LAYER(keycode);
            action.code  = ACTION_LAYER_MOMENTARY(action_layer);
            break;
        }
        case BK_LAYER_MOD_EXCLUSIVE ... BK_LAYER_MOD_EXCLUSIVE_MAX: {
            mod          = mod_config(BK_LAYER_MOD_EXCLUSIVE_GET_MODS(keycode));
            action_layer = BK_LAYER_MOD_EXCLUSIVE_GET_LAYER(keycode);
            action.code  = ACTION_LAYER_MODS(action_layer, (mod & 0x10) ? (mod & 0xF) << 4 : mod);
            break;
        }
    }

    return action;
}

// from quantum_keycodes.h, keymap_common.c, and action.c
bool process_records_argos_exclusive(uint16_t keycode, keyrecord_t *record){
    action_t action = bk_action_for_keycode(keycode);

    switch(keycode){
        case BK_MOMENTARY_EXCLUSIVE ... BK_MOMENTARY_EXCLUSIVE_MAX: {
            // TODO actually use action? process_action?
            uint16_t new_layer_index = BK_MOMENTARY_EXCLUSIVE_GET_LAYER(keycode);
            static uint8_t held_layer_index = 0;
            if(record->event.pressed) {
                // save current config
                held_layer_index = get_highest_layer(layer_state);
                // turn off all layers
                layer_clear();
                // trigger new layer
                layer_on(new_layer_index);
                LO_layer_state = layer_state;
                hold_layer = true;
            } else{
                // release layers
                layer_clear();
                hold_layer = false;
                // go back to old layer
                layer_on(held_layer_index);
            }
            return false;
            break;
        }
        case BK_LAYER_MOD_EXCLUSIVE ... BK_LAYER_MOD_EXCLUSIVE_MAX: {
            // calculate layer index:
            uint16_t new_layer_index = BK_LAYER_MOD_EXCLUSIVE_GET_LAYER(keycode);
            static uint8_t held_layer_index = 0;
            if(record->event.pressed) {
                // save current config
                held_layer_index = get_highest_layer(layer_state);
                // turn off all layers
                layer_clear();
                // trigger new layer
                layer_on(new_layer_index);
                LO_layer_state = layer_state;
                hold_layer = true;

                // trigger mod
                process_action(record, action);
                return false;
                break;
            } else{
                // release mod
                process_action(record, action);
                // clear all layers
                layer_clear();
                hold_layer = false;
                // go back to old layer
                layer_on(held_layer_index);
            }
            return false;
            break;
        }
    }

    return true;
}