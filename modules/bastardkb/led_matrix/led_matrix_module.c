// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "led_matrix_module.h"

ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 0, 0);

#include "hardware/platform_defs.h"
#if __has_include("argos_rgb.h")
#    include "argos_rgb.h"
#endif

#if !defined(MCU_RP)
#    error "LED matrix module bitbang is written for the RP2040"
#endif
#if SYS_CLK_KHZ != 125000
#    error "LED matrix module bitbang timings assume a 125 MHz system clock"
#endif

/*
 * Bit-banged WS2812 on LED_MATRIX_MODULE_PIN.
 *
 * `led_matrix_module_color` stores full-scale RGB. Brightness is applied while
 * copying into `led_matrix_module_wire` as GRB, which is what the LEDs expect.
 * The sender runs from SRAM and holds interrupts off for the frame (~6 ms).
 *
 * Thumb-16 shifts and adds are raw .hword encodings; this assembler rejects
 * the lsls/adds/subs mnemonics. Cycle counts at 125 MHz:
 *   0-bit high 50 cycles (400 ns), 1-bit high 101 cycles (808 ns)
 *   0-bit low ~108 cycles (864 ns), 1-bit low ~55 cycles (440 ns)
 */
static uint8_t led_matrix_module_color[LED_MATRIX_MODULE_LED_COUNT * 3];
static uint8_t led_matrix_module_wire[LED_MATRIX_MODULE_LED_COUNT * 3];
static uint8_t led_matrix_module_brightness = 255;

/* Physical grid. Index 0 is the bottom-right LED: even rows (from the bottom)
 * run right to left, odd rows run left to right. */
#define LED_MATRIX_MODULE_COLS 12
#define LED_MATRIX_MODULE_ROWS 16

/*
 * r0 = pin mask, r1 = byte count, r2 = GRB bytes.
 * The pointer is an asm input so the scaled buffer cannot be optimized away.
 */
static void __attribute__((noinline, noipa, section(".time_critical.led_matrix_module_send"))) led_matrix_module_send(uint32_t pin_mask, uint32_t byte_count, const uint8_t *data) {
    register uint32_t      mask asm("r0")  = pin_mask;
    register uint32_t      count asm("r1") = byte_count;
    register const uint8_t *ptr asm("r2")  = data;

    __asm__ volatile(
        "push {r4, r5, r6, r7}\n\t"
        "movs r4, r0\n\t" /* pin mask */
        "movs r5, r1\n\t" /* bytes left */
        "movs r6, r2\n\t" /* data pointer */
        "movs r0, #0xD0\n\t"
        ".hword 0x0600\n\t" /* lsls r0, r0, #24 -> SIO_BASE */
        "movs r1, r0\n\t"
        ".hword 0x3014\n\t" /* adds r0, #0x14 -> GPIO_OUT_SET */
        ".hword 0x3118\n\t" /* adds r1, #0x18 -> GPIO_OUT_CLR */
        ".hword 0x7837\n\t" /* ldrb r7, [r6] */
        ".hword 0x3601\n\t" /* adds r6, #1 */
        ".hword 0x063F\n\t" /* lsls r7, r7, #24 */
        "movs r2, #8\n\t"
        "1:\n\t"            /* next bit, MSB first */
        ".hword 0x007F\n\t" /* lsls r7, r7, #1 */
        "bcs 3f\n\t"
        "str r4, [r0]\n\t" /* 0-bit high */
        "movs r3, #45\n\t"
        "4:\n\t"
        ".hword 0x3B03\n\t" /* subs r3, #3 */
        "bcs 4b\n\t"
        "str r4, [r1]\n\t"
        "movs r3, #96\n\t" /* 0-bit low */
        "5:\n\t"
        ".hword 0x3B03\n\t" /* subs r3, #3 */
        "bcs 5b\n\t"
        "b 6f\n\t"
        "3:\n\t"
        "str r4, [r0]\n\t" /* 1-bit high */
        "movs r3, #96\n\t"
        "7:\n\t"
        ".hword 0x3B03\n\t" /* subs r3, #3 */
        "bcs 7b\n\t"
        "str r4, [r1]\n\t"
        "movs r3, #45\n\t" /* 1-bit low */
        "8:\n\t"
        ".hword 0x3B03\n\t" /* subs r3, #3 */
        "bcs 8b\n\t"
        "6:\n\t"
        ".hword 0x3A01\n\t" /* subs r2, #1  bit count */
        "bne 1b\n\t"
        ".hword 0x3D01\n\t" /* subs r5, #1  byte count */
        "beq 9f\n\t"
        ".hword 0x7837\n\t" /* ldrb r7, [r6] */
        ".hword 0x3601\n\t" /* adds r6, #1 */
        ".hword 0x063F\n\t" /* lsls r7, r7, #24 */
        "movs r2, #8\n\t"
        "b 1b\n\t"
        "9:\n\t"
        "pop {r4, r5, r6, r7}\n\t"
        : "+l"(mask), "+l"(count), "+l"(ptr)
        :
        : "r3", "r4", "r5", "r6", "r7", "cc", "memory");
}

void led_matrix_module_set_brightness(uint8_t brightness) {
    led_matrix_module_brightness = brightness;
}

void led_matrix_module_set_color(uint16_t index, uint8_t r, uint8_t g, uint8_t b) {
    if (index >= LED_MATRIX_MODULE_LED_COUNT) {
        return;
    }
    uint8_t *led = &led_matrix_module_color[index * 3];
    led[0]       = r;
    led[1]       = g;
    led[2]       = b;
}

/*
 * Keyboard brightness uses the same ratio as the per-key matrix,
 * rgb_matrix_get_val() / RGB_MATRIX_MAXIMUM_BRIGHTNESS. The module's own
 * control is squared first so it tracks perceived brightness: a linear 20%
 * (51/255) is still most of a WS2812's light.
 *
 * Supply and viewing derate come after that. The per-key LEDs are 3.3 V
 * behind frosted acrylic or keycaps. This strip is 5 V and viewed directly.
 * Supply: a WS2812 at 3.3 V draws about half the current it does at 5 V,
 * and blue/green fall off further, so keep 1/2. Viewing: frosted acrylic
 * and keycaps pass about a fifth of the light, so keep another 1/5.
 * At keyboard maximum and module brightness 255, a full-scale channel
 * lands near 25.
 */
#define LED_MATRIX_MODULE_SUPPLY_NUM 1
#define LED_MATRIX_MODULE_SUPPLY_DEN 2
#define LED_MATRIX_MODULE_VIEW_NUM   1
#define LED_MATRIX_MODULE_VIEW_DEN   5

static uint8_t led_matrix_module_scale(uint8_t component) {
    uint32_t level = ((uint32_t)led_matrix_module_brightness * led_matrix_module_brightness) / 255 / 4;
#    if defined(RGB_MATRIX_ENABLE)
    level = (level * rgb_matrix_get_val()) / RGB_MATRIX_MAXIMUM_BRIGHTNESS;
#    endif
    level = (level * LED_MATRIX_MODULE_SUPPLY_NUM * LED_MATRIX_MODULE_VIEW_NUM) / (LED_MATRIX_MODULE_SUPPLY_DEN * LED_MATRIX_MODULE_VIEW_DEN);
    return (uint8_t)(((uint32_t)component * level) / 255);
}

static void led_matrix_module_flush(void) {
    for (uint16_t i = 0; i < LED_MATRIX_MODULE_LED_COUNT; i++) {
        uint8_t *src = &led_matrix_module_color[i * 3];
        uint8_t *dst = &led_matrix_module_wire[i * 3];
        dst[0]       = led_matrix_module_scale(src[1]); /* G */
        dst[1]       = led_matrix_module_scale(src[0]); /* R */
        dst[2]       = led_matrix_module_scale(src[2]); /* B */
    }

    __asm__ volatile("" ::: "memory");
    __asm__ volatile("cpsid i" ::: "memory");
    led_matrix_module_send(1u << LED_MATRIX_MODULE_PIN, LED_MATRIX_MODULE_LED_COUNT * 3, led_matrix_module_wire);
    __asm__ volatile("cpsie i" ::: "memory");
    wait_us(280); /* latch / reset */
}

/* x = 0 is the left column, y = 0 is the bottom row. */
static uint16_t led_matrix_module_index(uint8_t x, uint8_t y) {
    if ((y & 1) == 0) {
        return (uint16_t)y * LED_MATRIX_MODULE_COLS + (LED_MATRIX_MODULE_COLS - 1 - x);
    }
    return (uint16_t)y * LED_MATRIX_MODULE_COLS + x;
}

/* Same color the layer digit uses, including the white stand-in for an uncolored layer. */
static RGB led_matrix_module_layer_color(void) {
    uint8_t layer = get_highest_layer(layer_state);
    if (layer > 9) {
        layer = 9;
    }

    RGB rgb = {0, 0, 0};
#    if (defined(RGBLIGHT_ENABLE) || defined(RGB_MATRIX_ENABLE)) && __has_include("argos_rgb.h")
    argos_rgb_get_layer_color(layer, &rgb);
#    endif
    if (rgb.r == 0 && rgb.g == 0 && rgb.b == 0) {
        rgb.r = 255;
        rgb.g = 255;
        rgb.b = 255;
    }
    return rgb;
}

/*
 * Typing-speed gauge, drawn in the three leftmost columns. The bottom row is
 * 0 WPM and green, the top row is 140 WPM and red, and the rows between take
 * the mix of those two. The bar fills upward to the current speed.
 *
 * A word is five keystrokes. Speed is how many of those fit in the last
 * three seconds, so the bar drops back to the green row once typing stops.
 * Modifier keys are ignored.
 */
#define LED_MATRIX_MODULE_WPM_MAX       140
#define LED_MATRIX_MODULE_WPM_WINDOW_MS 3000
#define LED_MATRIX_MODULE_WPM_SAMPLES   64
#define LED_MATRIX_MODULE_WPM_COLS      3

static uint32_t led_matrix_module_wpm_time[LED_MATRIX_MODULE_WPM_SAMPLES];
static uint8_t  led_matrix_module_wpm_head  = 0;
static uint8_t  led_matrix_module_wpm_count = 0;
static bool     led_matrix_module_wpm_dirty = false;

static void led_matrix_module_wpm_press(uint16_t keycode) {
    if (IS_MODIFIER_KEYCODE(keycode)) {
        return;
    }
    led_matrix_module_wpm_time[led_matrix_module_wpm_head] = timer_read32();
    led_matrix_module_wpm_head                             = (led_matrix_module_wpm_head + 1) % LED_MATRIX_MODULE_WPM_SAMPLES;
    if (led_matrix_module_wpm_count < LED_MATRIX_MODULE_WPM_SAMPLES) {
        led_matrix_module_wpm_count++;
    }
    led_matrix_module_wpm_dirty = true;
}

/* keys/5 words over a window of WINDOW_MS, in minutes. */
static uint16_t led_matrix_module_wpm(void) {
    uint16_t keys = 0;
    for (uint8_t n = 0; n < led_matrix_module_wpm_count; n++) {
        uint8_t i = (led_matrix_module_wpm_head + LED_MATRIX_MODULE_WPM_SAMPLES - 1 - n) % LED_MATRIX_MODULE_WPM_SAMPLES;
        if (timer_elapsed32(led_matrix_module_wpm_time[i]) >= LED_MATRIX_MODULE_WPM_WINDOW_MS) {
            break;
        }
        keys++;
    }
    return (uint16_t)((uint32_t)keys * 12000u / LED_MATRIX_MODULE_WPM_WINDOW_MS);
}

static void led_matrix_module_render_gauge(void) {
    uint16_t wpm = led_matrix_module_wpm();
    if (wpm > LED_MATRIX_MODULE_WPM_MAX) {
        wpm = LED_MATRIX_MODULE_WPM_MAX;
    }

    const uint8_t  span = LED_MATRIX_MODULE_ROWS - 1;
    const uint16_t pos  = wpm * span;

    for (uint8_t row = 0; row < LED_MATRIX_MODULE_ROWS; row++) {
        uint16_t mark       = (uint16_t)row * LED_MATRIX_MODULE_WPM_MAX;
        uint8_t  brightness = 0;
        if (row == 0 || pos >= mark) {
            brightness = 255;
        } else {
            uint16_t prev = (uint16_t)(row - 1) * LED_MATRIX_MODULE_WPM_MAX;
            if (pos > prev) {
                brightness = (uint8_t)((uint32_t)(pos - prev) * 255 / (mark - prev));
            }
        }

        uint8_t blend = (uint8_t)((uint16_t)row * 255 / span);
        uint8_t r     = (uint8_t)((uint16_t)blend * brightness / 255);
        uint8_t g     = (uint8_t)((uint16_t)(255 - blend) * brightness / 255);
        for (uint8_t col = 0; col < LED_MATRIX_MODULE_COLS; col++) {
            if (col < LED_MATRIX_MODULE_WPM_COLS) {
                led_matrix_module_set_color(led_matrix_module_index(col, row), r, g, 0);
            } else {
                led_matrix_module_set_color(led_matrix_module_index(col, row), 0, 0, 0);
            }
        }
    }
}

/* 5x7 glyphs. Bit 4 is the left pixel, row 0 is the top. Drawn at 2x, centered. */
static const uint8_t led_matrix_module_digit[10][7] = {
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, /* 0 */
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, /* 1 */
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, /* 2 */
    {0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E}, /* 3 */
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, /* 4 */
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, /* 5 */
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, /* 6 */
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, /* 7 */
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, /* 8 */
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}, /* 9 */
};

static void led_matrix_module_show_layer(void) {
    uint8_t layer = get_highest_layer(layer_state);
    if (layer == 0) {
        led_matrix_module_render_gauge();
        return;
    }

    for (uint16_t i = 0; i < LED_MATRIX_MODULE_LED_COUNT; i++) {
        led_matrix_module_set_color(i, 0, 0, 0);
    }

    if (layer > 9) {
        layer = 9;
    }

    RGB rgb = led_matrix_module_layer_color();

    const uint8_t scale    = 2;
    const uint8_t glyph_w  = 5;
    const uint8_t glyph_h  = 7;
    const uint8_t origin_x = (LED_MATRIX_MODULE_COLS - glyph_w * scale) / 2;
    const uint8_t origin_y = (LED_MATRIX_MODULE_ROWS - glyph_h * scale) / 2;

    for (uint8_t row = 0; row < glyph_h; row++) {
        uint8_t bits = led_matrix_module_digit[layer][row];
        for (uint8_t col = 0; col < glyph_w; col++) {
            if ((bits & (1u << (4 - col))) == 0) {
                continue;
            }
            for (uint8_t sy = 0; sy < scale; sy++) {
                for (uint8_t sx = 0; sx < scale; sx++) {
                    uint8_t x = origin_x + col * scale + sx;
                    uint8_t y = origin_y + (glyph_h - 1 - row) * scale + sy;
                    led_matrix_module_set_color(led_matrix_module_index(x, y), rgb.r, rgb.g, rgb.b);
                }
            }
        }
    }
}

/*
 * 3x4 glyphs for A-Z and 0-9. Bit 2 is the left pixel, row 0 is the top.
 * Cells are 4 columns apart and 5 rows apart (one dark pixel between glyphs),
 * so three glyphs fit across the 12-wide matrix and three fit down the 16-high
 * matrix with a one-pixel margin at the top and bottom. Reading order is
 * left to right, top to bottom.
 */
#define LED_MATRIX_MODULE_GLYPH_W       3
#define LED_MATRIX_MODULE_GLYPH_H       4
#define LED_MATRIX_MODULE_TEXT_COLS     3
#define LED_MATRIX_MODULE_TEXT_ROWS     3
#define LED_MATRIX_MODULE_TEXT_LEN      (LED_MATRIX_MODULE_TEXT_COLS * LED_MATRIX_MODULE_TEXT_ROWS)
#define LED_MATRIX_MODULE_TEXT_PITCH_X  4
#define LED_MATRIX_MODULE_TEXT_PITCH_Y  5
#define LED_MATRIX_MODULE_TEXT_ORIGIN_Y 1

static const uint8_t led_matrix_module_font[36][LED_MATRIX_MODULE_GLYPH_H] = {
    {0x7, 0x5, 0x5, 0x7}, /* 0  ### #.# #.# ### */
    {0x2, 0x6, 0x2, 0x7}, /* 1  .#. ##. .#. ### */
    {0x7, 0x1, 0x6, 0x7}, /* 2  ### ..# ##. ### */
    {0x7, 0x1, 0x3, 0x7}, /* 3  ### ..# .## ### */
    {0x5, 0x5, 0x7, 0x1}, /* 4  #.# #.# ### ..# */
    {0x7, 0x4, 0x3, 0x7}, /* 5  ### #.. .## ### */
    {0x6, 0x4, 0x7, 0x5}, /* 6  ##. #.. ### #.# */
    {0x7, 0x1, 0x2, 0x2}, /* 7  ### ..# .#. .#. */
    {0x7, 0x5, 0x7, 0x7}, /* 8  ### #.# ### ### */
    {0x7, 0x5, 0x7, 0x1}, /* 9  ### #.# ### ..# */
    {0x2, 0x5, 0x7, 0x5}, /* A  .#. #.# ### #.# */
    {0x6, 0x5, 0x6, 0x6}, /* B  ##. #.# ##. ##. */
    {0x3, 0x4, 0x4, 0x3}, /* C  .## #.. #.. .## */
    {0x6, 0x5, 0x5, 0x6}, /* D  ##. #.# #.# ##. */
    {0x7, 0x6, 0x4, 0x7}, /* E  ### ##. #.. ### */
    {0x7, 0x6, 0x4, 0x4}, /* F  ### ##. #.. #.. */
    {0x3, 0x4, 0x5, 0x3}, /* G  .## #.. #.# .## */
    {0x5, 0x7, 0x5, 0x5}, /* H  #.# ### #.# #.# */
    {0x7, 0x2, 0x2, 0x7}, /* I  ### .#. .#. ### */
    {0x1, 0x1, 0x5, 0x2}, /* J  ..# ..# #.# .#. */
    {0x5, 0x6, 0x5, 0x5}, /* K  #.# ##. #.# #.# */
    {0x4, 0x4, 0x4, 0x7}, /* L  #.. #.. #.. ### */
    {0x5, 0x7, 0x7, 0x5}, /* M  #.# ### ### #.# */
    {0x6, 0x5, 0x5, 0x5}, /* N  ##. #.# #.# #.# */
    {0x2, 0x5, 0x5, 0x2}, /* O  .#. #.# #.# .#. */
    {0x6, 0x5, 0x6, 0x4}, /* P  ##. #.# ##. #.. */
    {0x2, 0x5, 0x3, 0x1}, /* Q  .#. #.# .## ..# */
    {0x6, 0x5, 0x6, 0x5}, /* R  ##. #.# ##. #.# */
    {0x3, 0x4, 0x1, 0x6}, /* S  .## #.. ..# ##. */
    {0x7, 0x2, 0x2, 0x2}, /* T  ### .#. .#. .#. */
    {0x5, 0x5, 0x5, 0x7}, /* U  #.# #.# #.# ### */
    {0x5, 0x5, 0x5, 0x2}, /* V  #.# #.# #.# .#. */
    {0x5, 0x5, 0x7, 0x5}, /* W  #.# #.# ### #.# */
    {0x5, 0x2, 0x2, 0x5}, /* X  #.# .#. .#. #.# */
    {0x5, 0x5, 0x2, 0x2}, /* Y  #.# #.# .#. .#. */
    {0x7, 0x1, 0x4, 0x7}, /* Z  ### ..# #.. ### */
};

static char    led_matrix_module_text[LED_MATRIX_MODULE_TEXT_LEN];
static uint8_t led_matrix_module_text_len = 0;
static bool    led_matrix_module_text_set = false;

static int8_t led_matrix_module_glyph_index(char c) {
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - ('a' - 'A'));
    }
    if (c >= '0' && c <= '9') {
        return (int8_t)(c - '0');
    }
    if (c >= 'A' && c <= 'Z') {
        return (int8_t)(c - 'A' + 10);
    }
    return -1;
}

static void led_matrix_module_draw_glyph(uint8_t origin_x, uint8_t origin_y, uint8_t glyph, uint8_t r, uint8_t g, uint8_t b) {
    for (uint8_t row = 0; row < LED_MATRIX_MODULE_GLYPH_H; row++) {
        uint8_t bits = led_matrix_module_font[glyph][row];
        for (uint8_t col = 0; col < LED_MATRIX_MODULE_GLYPH_W; col++) {
            if ((bits & (1u << (LED_MATRIX_MODULE_GLYPH_W - 1 - col))) == 0) {
                continue;
            }
            uint8_t x = origin_x + col;
            uint8_t y = origin_y + (LED_MATRIX_MODULE_GLYPH_H - 1 - row);
            led_matrix_module_set_color(led_matrix_module_index(x, y), r, g, b);
        }
    }
}

static void led_matrix_module_render_text(const char *text, uint8_t len, uint8_t r, uint8_t g, uint8_t b) {
    for (uint16_t i = 0; i < LED_MATRIX_MODULE_LED_COUNT; i++) {
        led_matrix_module_set_color(i, 0, 0, 0);
    }

    for (uint8_t i = 0; i < len; i++) {
        int8_t glyph = led_matrix_module_glyph_index(text[i]);
        if (glyph < 0) {
            continue;
        }
        uint8_t col = i % LED_MATRIX_MODULE_TEXT_COLS;
        uint8_t row = i / LED_MATRIX_MODULE_TEXT_COLS;
        uint8_t x   = col * LED_MATRIX_MODULE_TEXT_PITCH_X;
        uint8_t y   = LED_MATRIX_MODULE_TEXT_ORIGIN_Y + (LED_MATRIX_MODULE_TEXT_ROWS - 1 - row) * LED_MATRIX_MODULE_TEXT_PITCH_Y;
        led_matrix_module_draw_glyph(x, y, (uint8_t)glyph, r, g, b);
    }
}

/* Held modifiers, in the order they were pressed. Left and right of a kind share one row. */
#define LED_MATRIX_MOD_COUNT 4

static const uint8_t led_matrix_mod_mask[LED_MATRIX_MOD_COUNT] = {
    MOD_MASK_SHIFT,
    MOD_MASK_CTRL,
    MOD_MASK_GUI,
    MOD_MASK_ALT,
};

static const char led_matrix_mod_label[LED_MATRIX_MOD_COUNT][4] = {
    "SFT",
    "CTL",
    "GUI",
    "ALT",
};

static uint8_t led_matrix_mod_stack[LED_MATRIX_MOD_COUNT];
static uint8_t led_matrix_mod_stack_len = 0;

static bool led_matrix_module_sync_mods(void) {
    uint8_t mods = get_mods();
    uint8_t next[LED_MATRIX_MOD_COUNT];
    uint8_t next_len = 0;

    for (uint8_t i = 0; i < led_matrix_mod_stack_len; i++) {
        uint8_t mod = led_matrix_mod_stack[i];
        if (mods & led_matrix_mod_mask[mod]) {
            next[next_len++] = mod;
        }
    }
    for (uint8_t mod = 0; mod < LED_MATRIX_MOD_COUNT; mod++) {
        if ((mods & led_matrix_mod_mask[mod]) == 0) {
            continue;
        }
        bool already = false;
        for (uint8_t i = 0; i < next_len; i++) {
            if (next[i] == mod) {
                already = true;
                break;
            }
        }
        if (!already) {
            next[next_len++] = mod;
        }
    }

    bool changed = next_len != led_matrix_mod_stack_len;
    for (uint8_t i = 0; i < next_len; i++) {
        if (!changed && next[i] != led_matrix_mod_stack[i]) {
            changed = true;
        }
        led_matrix_mod_stack[i] = next[i];
    }
    led_matrix_mod_stack_len = next_len;
    return changed;
}

static void led_matrix_module_render_mods(void) {
    char    text[LED_MATRIX_MODULE_TEXT_LEN];
    uint8_t len  = 0;
    uint8_t rows = led_matrix_mod_stack_len;
    if (rows > LED_MATRIX_MODULE_TEXT_ROWS) {
        rows = LED_MATRIX_MODULE_TEXT_ROWS;
    }
    for (uint8_t i = 0; i < rows; i++) {
        const char *label = led_matrix_mod_label[led_matrix_mod_stack[i]];
        text[len++]       = label[0];
        text[len++]       = label[1];
        text[len++]       = label[2];
    }
    RGB rgb = led_matrix_module_layer_color();
    led_matrix_module_render_text(text, len, rgb.r, rgb.g, rgb.b);
}

void led_matrix_module_show_text(const char *text) {
    led_matrix_module_text_len = 0;
    led_matrix_module_text_set = true;
    if (text != NULL) {
        while (led_matrix_module_text_len < LED_MATRIX_MODULE_TEXT_LEN && text[led_matrix_module_text_len] != '\0') {
            led_matrix_module_text[led_matrix_module_text_len] = text[led_matrix_module_text_len];
            led_matrix_module_text_len++;
        }
    }
    led_matrix_module_render_text(led_matrix_module_text, led_matrix_module_text_len, 255, 255, 255);
    led_matrix_module_flush();
}

void keyboard_post_init_led_matrix(void) {
    gpio_set_pin_output(LED_MATRIX_MODULE_PIN);
    gpio_write_pin_low(LED_MATRIX_MODULE_PIN);
    wait_us(280); /* reset the strip before the first frame */
}

bool process_record_led_matrix(uint16_t keycode, keyrecord_t *record) {
    if (record->event.pressed) {
        led_matrix_module_wpm_press(keycode);
    }
    return true;
}

void housekeeping_task_led_matrix(void) {
    static uint32_t last_update = 0;

    bool mods_changed = led_matrix_module_sync_mods();
    if (!mods_changed && !led_matrix_module_wpm_dirty && timer_elapsed32(last_update) < LED_MATRIX_MODULE_REFRESH_MS) {
        return;
    }
    led_matrix_module_wpm_dirty = false;
    last_update                  = timer_read32();
    if (led_matrix_mod_stack_len > 0) {
        led_matrix_module_render_mods();
    } else if (led_matrix_module_text_set) {
        led_matrix_module_render_text(led_matrix_module_text, led_matrix_module_text_len, 255, 255, 255);
    } else {
        led_matrix_module_show_layer();
    }
    led_matrix_module_flush();
}

