// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "argos_led_module.h"

#ifndef ARGOS_LED_MODULE_ENABLE

void argos_led_module_init(void) {}

void argos_led_module_task(void) {}

void argos_led_module_set_brightness(uint8_t brightness) {
    (void)brightness;
}

void argos_led_module_set_color(uint16_t index, uint8_t r, uint8_t g, uint8_t b) {
    (void)index;
    (void)r;
    (void)g;
    (void)b;
}

#else

#    include "hardware/platform_defs.h"
#    include "argos_rgb.h"

#    if !defined(MCU_RP)
#        error "Argos LED module bitbang is written for the RP2040"
#    endif
#    if SYS_CLK_KHZ != 125000
#        error "Argos LED module bitbang timings assume a 125 MHz system clock"
#    endif

/*
 * Bit-banged WS2812 on ARGOS_LED_MODULE_PIN.
 *
 * `argos_led_module_color` stores full-scale RGB. Brightness is applied while
 * copying into `argos_led_module_wire` as GRB, which is what the LEDs expect.
 * The sender runs from SRAM and holds interrupts off for the frame (~6 ms).
 *
 * Thumb-16 shifts and adds are raw .hword encodings; this assembler rejects
 * the lsls/adds/subs mnemonics. Cycle counts at 125 MHz:
 *   0-bit high 50 cycles (400 ns), 1-bit high 101 cycles (808 ns)
 *   0-bit low ~108 cycles (864 ns), 1-bit low ~55 cycles (440 ns)
 */
static uint8_t argos_led_module_color[ARGOS_LED_MODULE_LED_COUNT * 3];
static uint8_t argos_led_module_wire[ARGOS_LED_MODULE_LED_COUNT * 3];
static uint8_t argos_led_module_brightness = 255;

/* Physical grid. Index 0 is the bottom-right LED: even rows (from the bottom)
 * run right to left, odd rows run left to right. */
#define ARGOS_LED_MODULE_COLS 12
#define ARGOS_LED_MODULE_ROWS 16

/*
 * r0 = pin mask, r1 = byte count, r2 = GRB bytes.
 * The pointer is an asm input so the scaled buffer cannot be optimized away.
 */
static void __attribute__((noinline, noipa, section(".time_critical.argos_led_module_send"))) argos_led_module_send(uint32_t pin_mask, uint32_t byte_count, const uint8_t *data) {
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

void argos_led_module_set_brightness(uint8_t brightness) {
    argos_led_module_brightness = brightness;
}

void argos_led_module_set_color(uint16_t index, uint8_t r, uint8_t g, uint8_t b) {
    if (index >= ARGOS_LED_MODULE_LED_COUNT) {
        return;
    }
    uint8_t *led = &argos_led_module_color[index * 3];
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
#define ARGOS_LED_MODULE_SUPPLY_NUM 1
#define ARGOS_LED_MODULE_SUPPLY_DEN 2
#define ARGOS_LED_MODULE_VIEW_NUM   1
#define ARGOS_LED_MODULE_VIEW_DEN   5

static uint8_t argos_led_module_scale(uint8_t component) {
    uint32_t level = ((uint32_t)argos_led_module_brightness * argos_led_module_brightness) / 255 / 4;
#    if defined(RGB_MATRIX_ENABLE)
    level = (level * rgb_matrix_get_val()) / RGB_MATRIX_MAXIMUM_BRIGHTNESS;
#    endif
    level = (level * ARGOS_LED_MODULE_SUPPLY_NUM * ARGOS_LED_MODULE_VIEW_NUM) / (ARGOS_LED_MODULE_SUPPLY_DEN * ARGOS_LED_MODULE_VIEW_DEN);
    return (uint8_t)(((uint32_t)component * level) / 255);
}

static void argos_led_module_flush(void) {
    for (uint16_t i = 0; i < ARGOS_LED_MODULE_LED_COUNT; i++) {
        uint8_t *src = &argos_led_module_color[i * 3];
        uint8_t *dst = &argos_led_module_wire[i * 3];
        dst[0]       = argos_led_module_scale(src[1]); /* G */
        dst[1]       = argos_led_module_scale(src[0]); /* R */
        dst[2]       = argos_led_module_scale(src[2]); /* B */
    }

    __asm__ volatile("" ::: "memory");
    __asm__ volatile("cpsid i" ::: "memory");
    argos_led_module_send(1u << ARGOS_LED_MODULE_PIN, ARGOS_LED_MODULE_LED_COUNT * 3, argos_led_module_wire);
    __asm__ volatile("cpsie i" ::: "memory");
    wait_us(280); /* latch / reset */
}

/* x = 0 is the left column, y = 0 is the bottom row. */
static uint16_t argos_led_module_index(uint8_t x, uint8_t y) {
    if ((y & 1) == 0) {
        return (uint16_t)y * ARGOS_LED_MODULE_COLS + (ARGOS_LED_MODULE_COLS - 1 - x);
    }
    return (uint16_t)y * ARGOS_LED_MODULE_COLS + x;
}

/* 5x7 glyphs. Bit 4 is the left pixel, row 0 is the top. Drawn at 2x, centered. */
static const uint8_t argos_led_module_digit[10][7] = {
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

static void argos_led_module_show_layer(void) {
    for (uint16_t i = 0; i < ARGOS_LED_MODULE_LED_COUNT; i++) {
        argos_led_module_set_color(i, 0, 0, 0);
    }

    uint8_t layer = get_highest_layer(layer_state);
    if (layer > 9) {
        layer = 9;
    }

    RGB rgb = {0, 0, 0};
#    if defined(RGBLIGHT_ENABLE) || defined(RGB_MATRIX_ENABLE)
    argos_rgb_get_layer_color(layer, &rgb);
#    endif
    /* Layer 0 is stored as off, which would draw an invisible digit. */
    if (rgb.r == 0 && rgb.g == 0 && rgb.b == 0) {
        rgb.r = 255;
        rgb.g = 255;
        rgb.b = 255;
    }

    const uint8_t scale    = 2;
    const uint8_t glyph_w  = 5;
    const uint8_t glyph_h  = 7;
    const uint8_t origin_x = (ARGOS_LED_MODULE_COLS - glyph_w * scale) / 2;
    const uint8_t origin_y = (ARGOS_LED_MODULE_ROWS - glyph_h * scale) / 2;

    for (uint8_t row = 0; row < glyph_h; row++) {
        uint8_t bits = argos_led_module_digit[layer][row];
        for (uint8_t col = 0; col < glyph_w; col++) {
            if ((bits & (1u << (4 - col))) == 0) {
                continue;
            }
            for (uint8_t sy = 0; sy < scale; sy++) {
                for (uint8_t sx = 0; sx < scale; sx++) {
                    uint8_t x = origin_x + col * scale + sx;
                    uint8_t y = origin_y + (glyph_h - 1 - row) * scale + sy;
                    argos_led_module_set_color(argos_led_module_index(x, y), rgb.r, rgb.g, rgb.b);
                }
            }
        }
    }
}

void argos_led_module_init(void) {
    gpio_set_pin_output(ARGOS_LED_MODULE_PIN);
    gpio_write_pin_low(ARGOS_LED_MODULE_PIN);
    wait_us(280); /* reset the strip before the first frame */
}

void argos_led_module_task(void) {
    static uint32_t last_update = 0;

    if (timer_elapsed32(last_update) >= ARGOS_LED_MODULE_REFRESH_MS) {
        last_update = timer_read32();
        argos_led_module_show_layer();
        argos_led_module_flush();
    }
}

#endif
