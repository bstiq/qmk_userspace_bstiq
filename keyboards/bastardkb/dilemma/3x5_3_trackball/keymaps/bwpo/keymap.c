/**
 * Copyright 2022 Charly Delay <charly@codesink.dev> (@0xcharly)
 * Copyright 2023 casuanoob <casuanoob@hotmail.com> (@casuanoob)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
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

#include "config.h"
// #include "bk_pointing_device.h" // TODO remove this, should be included from the module
#include QMK_KEYBOARD_H

enum dilemma_keymap_layers {
    LAYER_BASE = 0,
    LAYER_FUNCTION,
    LAYER_NAVIGATION,
    LAYER_MEDIA,
    LAYER_POINTER,
    LAYER_NUMERAL,
    LAYER_SYMBOLS,
    LAYER_LCD,
};

enum custom_keycodes {
    QK_REG = SAFE_RANGE,
    QK_HELP
};

const uint16_t PROGMEM combo1[] = { RSFT_T(KC_I), LCTL_T(KC_E), COMBO_END };
const uint16_t PROGMEM combo2[] = { RSFT_T(KC_S), LCTL_T(KC_T), COMBO_END };
const uint16_t PROGMEM combo3[] = { KC_P, KC_O, COMBO_END };
const uint16_t PROGMEM combo4[] = { KC_V, KC_D, COMBO_END };
const uint16_t PROGMEM combo5[] = { KC_SLSH, KC_X, COMBO_END };
const uint16_t PROGMEM combo6[] = { KC_Q, KC_G, COMBO_END };

combo_t key_combos[] = {
    COMBO(combo1, KC_LPRN),
    COMBO(combo2, KC_RPRN),
    COMBO(combo3, KC_LBRC),
    COMBO(combo4, KC_RBRC),
    COMBO(combo5, LSFT(KC_LBRC)),
    COMBO(combo6, LSFT(KC_RBRC)),
};
// Automatically enable sniping-mode on the pointer layer.
// #define DILEMMA_AUTO_SNIPING_ON_LAYER LAYER_POINTER

#define ESC_MED LT(LAYER_MEDIA, KC_ESC)
#define SPC_NAV LT(LAYER_NAVIGATION, KC_SPC)
#define TAB_FUN LT(LAYER_FUNCTION, KC_TAB)
#define TAB_NAV LT(LAYER_NAVIGATION, KC_TAB)
#define ENT_SYM LT(LAYER_SYMBOLS, KC_ENT)
#define BSP_NUM LT(LAYER_NUMERAL, KC_BSPC)
#define PT_Z LT(LAYER_POINTER, KC_Z)
#define PT_SLSH LT(LAYER_POINTER, KC_SLSH)
#define ESC_MED LT(LAYER_MEDIA, KC_ESC)
#define SPC_NAV LT(LAYER_NAVIGATION, KC_SPC)
#define TAB_FUN LT(LAYER_FUNCTION, KC_TAB)
#define ENT_SYM LT(LAYER_SYMBOLS, KC_ENT)
#define BSP_NUM LT(LAYER_NUMERAL, KC_BSPC)
#define SPC_NUM LT(LAYER_NUMERAL, KC_SPC)
#define _L_PTR(KC) LT(LAYER_POINTER, KC)

#ifndef POINTING_DEVICE_ENABLE
#    define DRGSCRL KC_NO
#    define DPI_MOD KC_NO
#    define S_D_MOD KC_NO
#    define SNIPING KC_NO
#endif // !POINTING_DEVICE_ENABLE

#include "keymap.h"

// clang-format off
/** \brief QWERTY layout (3 rows, 10 columns). */
#define LAYOUT_LAYER_BASE                                                                     \
       KC_B,    KC_W,    KC_P,    KC_O,    KC_QUOT,    KC_DOT,    KC_V,    KC_D,    KC_L,    KC_J, \
       KC_A,    KC_U,    KC_I,    KC_E,    KC_COMM,    KC_C,    KC_T,    KC_S,    KC_R, KC_N, \
       KC_Z,    KC_Y,    KC_X,    KC_SLSH,    KC_K,    KC_M,    KC_Q, KC_G,  KC_H, KC_F, \
                      MO(LAYER_LCD), ESC_MED, TAB_NAV, SPC_NUM, ENT_SYM, MO(LAYER_LCD)

/** Convenience row shorthands. */
#define _______________DEAD_HALF_ROW_______________ XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX
#define ______________HOME_ROW_GACS_L______________ KC_LGUI, KC_LALT, KC_LSFT, KC_LCTL, XXXXXXX
#define ______________HOME_ROW_GACS_R______________ XXXXXXX, KC_LCTL, KC_LSFT, KC_LALT, KC_LGUI

/*
 * Layers used on the Charybdis Nano.
 *
 * These layers started off heavily inspired by the Miryoku layout, but trimmed
 * down and tailored for a stock experience that is meant to be fundation for
 * further personalization.
 *
 * See https://github.com/manna-harbour/miryoku for the original layout.
 */

 /**
  * \brief Function layer.
  *
  * Secondary right-hand layer has function keys mirroring the numerals on the
  * primary layer with extras on the pinkie column, plus system keys on the inner
  * column. App is on the tertiary thumb key and other thumb keys are duplicated
  * from the base layer to enable auto-repeat.
  */
#define LAYOUT_LAYER_FUNCTION                                                                 \
    _______________DEAD_HALF_ROW_______________, KC_PSCR,   KC_F7,   KC_F8,   KC_F9,  KC_F12, \
    ______________HOME_ROW_GACS_L______________, KC_SCRL,   KC_F4,   KC_F5,   KC_F6,  KC_F11, \
    _______________DEAD_HALF_ROW_______________, KC_PAUS,   KC_F1,   KC_F2,   KC_F3,  KC_F10, \
                      XXXXXXX, XXXXXXX, _______, XXXXXXX, XXXXXXX, _______

  /**
   * \brief Media layer.
   *   
   * Tertiary left- and right-hand layer is media and RGB control.  This layer is
   * symmetrical to accomodate the left- and right-hand trackball.
   */
#define LAYOUT_LAYER_MEDIA                                                                    \
    XXXXXXX,XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, \
    KC_MPRV, KC_VOLD, KC_MUTE, KC_VOLU, KC_MNXT, KC_MPRV, KC_VOLD, KC_MUTE, KC_VOLU, KC_MNXT, \
    XXXXXXX, XXXXXXX, XXXXXXX,  EE_CLR, QK_BOOT, QK_BOOT,  EE_CLR, XXXXXXX, XXXXXXX, XXXXXXX, \
                      _______, KC_MPLY, KC_MSTP, KC_MSTP, KC_MPLY, _______

   /** \brief Mouse emulation and pointer functions. */
#define LAYOUT_LAYER_POINTER                                                                  \
    QK_BOOT,  EE_CLR, XXXXXXX, DPI_MOD, S_D_MOD, S_D_MOD, DPI_MOD, XXXXXXX,  EE_CLR, QK_BOOT, \
    ______________HOME_ROW_GACS_L______________, ______________HOME_ROW_GACS_R______________, \
    _______, DRGSCRL, SNIPING, CURSOR, XXXXXXX, XXXXXXX, XXXXXXX, SNIPING, DRGSCRL, _______, \
                      MS_BTN2, MS_BTN3, MS_BTN1, MS_BTN3, MS_BTN1, _______

/**
 * \brief Navigation layer.
 *
 * Primary right-hand layer (left home thumb) is navigation and editing. Cursor
 * keys are on the home position, line and page movement below, clipboard above,
 * caps lock and insert on the inner column. Thumb keys are duplicated from the
 * base layer to avoid having to layer change mid edit and to enable auto-repeat.
 */
#define LAYOUT_LAYER_NAVIGATION                                                               \
    _______, _______, _______, _______, _______, _______________DEAD_HALF_ROW_______________, \
    ______________HOME_ROW_GACS_L______________, KC_BSPC, KC_LEFT, KC_DOWN,   KC_UP, KC_RGHT, \
    _______________DEAD_HALF_ROW_______________,  KC_DEL, KC_HOME, KC_PGDN, KC_PGUP,  KC_END, \
                      XXXXXXX, _______, XXXXXXX,  KC_ENT, KC_ENT, _______

#define LAYOUT_LAYER_LCD                                                             \
    QK_REG, _______, _______, _______, QK_HELP, _______________DEAD_HALF_ROW_______________, \
    _______, _______, _______, _______, _______ , _______________DEAD_HALF_ROW_______________, \
    _______________DEAD_HALF_ROW_______________,  _______________DEAD_HALF_ROW_______________, \
                      XXXXXXX, _______, XXXXXXX,  _______, _______, _______
 /**
  * \brief Numeral layout.
  *
  * Primary left-hand layer (right home thumb) is numerals and symbols. Numerals
  * are in the standard numpad locations with symbols in the remaining positions.
  * `KC_DOT` is duplicated from the base layer.
  */
#define LAYOUT_LAYER_NUMERAL                                                                  \
    KC_LBRC,    KC_7,    KC_8,    KC_9, KC_RBRC, _______________DEAD_HALF_ROW_______________, \
    KC_SCLN,    KC_4,    KC_5,    KC_6,  KC_EQL, ______________HOME_ROW_GACS_R______________, \
    KC_GRV,     KC_1,    KC_2,    KC_3, KC_BSLS, _______________DEAD_HALF_ROW_______________, \
                       KC_DOT,    KC_MINS, KC_0, XXXXXXX, _______, _______

  /**
   * \brief Symbols layer.
   *
   * Secondary left-hand layer has shifted symbols in the same locations to reduce
   * chording when using mods with shifted symbols. `KC_LPRN` is duplicated next to
   * `KC_RPRN`.
   */
#define LAYOUT_LAYER_SYMBOLS                                                                  \
    KC_LCBR, KC_AMPR, KC_ASTR, KC_LPRN, KC_RCBR, _______________DEAD_HALF_ROW_______________, \
    KC_COLN,  KC_DLR, KC_PERC, KC_CIRC, KC_PLUS, ______________HOME_ROW_GACS_R______________, \
    KC_TILD, KC_EXLM,   KC_AT, KC_HASH, KC_PIPE, _______________DEAD_HALF_ROW_______________, \
                      KC_UNDS, KC_LPRN, KC_RPRN, _______, XXXXXXX, _______

   /**
    * \brief Add Home Row mod to a layout.
    *
    * Expects a 10-key per row layout.  Adds support for GACS (Gui, Alt, Ctl, Shift)
    * home row.  The layout passed in parameter must contain at least 20 keycodes.
    *
    * This is meant to be used with `LAYER_ALPHAS_QWERTY` defined above, eg.:
    *
    *     HOME_ROW_MOD_GACS(LAYER_ALPHAS_QWERTY)
    */
#define _HOME_ROW_MOD_GACS(                                            \
    L00, L01, L02, L03, L04, R05, R06, R07, R08, R09,                  \
    L10, L11, L12, L13, L14, R15, R16, R17, R18, R19,                  \
    ...)                                                               \
             L00,         L01,         L02,         L03,         L04,  \
             R05,         R06,         R07,         R08,         R09,  \
      LGUI_T(L10), LALT_T(L11), RSFT_T(L12), LCTL_T(L13),        L14,  \
             R15,  LCTL_T(R16), RSFT_T(R17), LALT_T(R18), RGUI_T(R19), \
      __VA_ARGS__
#define HOME_ROW_MOD_GACS(...) _HOME_ROW_MOD_GACS(__VA_ARGS__)

    /**
     * \brief Add pointer layer keys to a layout.
     *
     * Expects a 10-key per row layout.  The layout passed in parameter must contain
     * at least 30 keycodes.
     *
     * This is meant to be used with `LAYER_ALPHAS_QWERTY` defined above, eg.:
     *
     *     POINTER_MOD(LAYER_ALPHAS_QWERTY)
     */
#define _POINTER_MOD(                                                  \
    L00, L01, L02, L03, L04, R05, R06, R07, R08, R09,                  \
    L10, L11, L12, L13, L14, R15, R16, R17, R18, R19,                  \
    L20, L21, L22, L23, L24, R25, R26, R27, R28, R29,                  \
    ...)                                                               \
             L00,         L01,         L02,         L03,         L04,  \
             R05,         R06,         R07,         R08,         R09,  \
             L10,         L11,         L12,         L13,         L14,  \
             R15,         R16,         R17,         R18,         R19,  \
      _L_PTR(L20),        L21,         L22,         L23,         L24,  \
             R25,         R26,         R27,         R28,  _L_PTR(R29), \
      __VA_ARGS__
#define POINTER_MOD(...) _POINTER_MOD(__VA_ARGS__)

#define LAYOUT_wrapper(...) LAYOUT_split_3x5_3(__VA_ARGS__)

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
  [LAYER_BASE] = LAYOUT_wrapper(
    POINTER_MOD(HOME_ROW_MOD_GACS(LAYOUT_LAYER_BASE))
  ),
  [LAYER_FUNCTION] = LAYOUT_wrapper(LAYOUT_LAYER_FUNCTION),
  [LAYER_NAVIGATION] = LAYOUT_wrapper(LAYOUT_LAYER_NAVIGATION),
  [LAYER_MEDIA] = LAYOUT_wrapper(LAYOUT_LAYER_MEDIA),
  [LAYER_NUMERAL] = LAYOUT_wrapper(LAYOUT_LAYER_NUMERAL),
  [LAYER_POINTER] = LAYOUT_wrapper(LAYOUT_LAYER_POINTER),
  [LAYER_SYMBOLS] = LAYOUT_wrapper(LAYOUT_LAYER_SYMBOLS),
  [LAYER_LCD] = LAYOUT_wrapper(LAYOUT_LAYER_LCD),
};
// clang-format on

#ifdef POINTING_DEVICE_ENABLE
#    ifdef DILEMMA_AUTO_SNIPING_ON_LAYER
layer_state_t layer_state_set_user(layer_state_t state) {
    dilemma_set_pointer_sniping_enabled(layer_state_cmp(state, DILEMMA_AUTO_SNIPING_ON_LAYER));
    return state;
}
#    endif // DILEMMA_AUTO_SNIPING_ON_LAYER
#endif     // POINTING_DEVICE_ENABLE

#ifdef ENCODER_MAP_ENABLE
// clang-format off
const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][NUM_DIRECTIONS] = {
    [LAYER_BASE]       = {ENCODER_CCW_CW(MS_WHLD, MS_WHLU),  ENCODER_CCW_CW(KC_VOLD, KC_VOLU)},
    [LAYER_FUNCTION]   = {ENCODER_CCW_CW(KC_DOWN, KC_UP),    ENCODER_CCW_CW(KC_LEFT, KC_RGHT)},
    [LAYER_NAVIGATION] = {ENCODER_CCW_CW(KC_PGDN, KC_PGUP),  ENCODER_CCW_CW(KC_VOLU, KC_VOLD)},
    [LAYER_MEDIA]      = {ENCODER_CCW_CW(KC_PGDN, KC_PGUP),  ENCODER_CCW_CW(KC_VOLU, KC_VOLD)},
    [LAYER_POINTER]    = {ENCODER_CCW_CW(RM_HUED, RM_HUEU),  ENCODER_CCW_CW(RM_SATD, RM_SATU)},
    [LAYER_NUMERAL]    = {ENCODER_CCW_CW(RM_VALD, RM_VALU),  ENCODER_CCW_CW(RM_SPDD, RM_SPDU)},
    [LAYER_SYMBOLS]    = {ENCODER_CCW_CW(RM_PREV, RM_NEXT),  ENCODER_CCW_CW(KC_LEFT, KC_RGHT)},
};

bool process_record_user(uint16_t keycode, keyrecord_t* record) {
    switch (keycode) {
        case QK_REG:
            if (record->event.pressed) {
                SEND_STRING_DELAY("Regards,\nQuentin Lebastard\nBastard Keyboards", 5);
            }
            break;
        case QK_HELP:
            if (record->event.pressed) {
                SEND_STRING_DELAY("I hope this helps, let me know if there's anything else!", 5);
            }
            break;
    }
    return true;
};

// clang-format on
#endif // ENCODER_MAP_ENABLE