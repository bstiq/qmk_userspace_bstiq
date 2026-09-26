// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "led_matrix_display.h"

ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 0, 0);

#if !defined(MCU_RP)
#    error "LED matrix module bitbang is written for the RP2040"
#endif
#if SYS_CLK_KHZ != 125000
#    error "LED matrix module bitbang timings assume a 125 MHz system clock"
#endif

/*
 * Bit-banged WS2812 on LED_MATRIX_MODULE_PIN.
 *
 * bklm_show receives full-scale RGB in visual order. Brightness is applied
 * while copying into bklm_wire as GRB, which is what the LEDs expect.
 * The sender runs from SRAM and holds interrupts off for the frame (~6 ms).
 *
 * Thumb-16 shifts and adds are raw .hword encodings; this assembler rejects
 * the lsls/adds/subs mnemonics. Cycle counts at 125 MHz:
 *   0-bit high 50 cycles (400 ns), 1-bit high 101 cycles (808 ns)
 *   0-bit low ~108 cycles (864 ns), 1-bit low ~55 cycles (440 ns)
 */
static uint8_t bklm_wire[LED_MATRIX_MODULE_LED_COUNT * 3];
static uint8_t bklm_brightness = 255;

_Static_assert(LED_MATRIX_MODULE_LED_COUNT == BKLM_COLS * BKLM_ROWS, "LED matrix geometry does not match LED_MATRIX_MODULE_LED_COUNT");

/*
 * r0 = pin mask, r1 = byte count, r2 = GRB bytes.
 * The pointer is an asm input so the scaled buffer cannot be optimized away.
 */
/* Bit-bangs one GRB frame. The caller disables interrupts for the duration. */
static void __attribute__((noinline, noipa, section(".time_critical.bklm_send"))) bklm_send(uint32_t pin_mask, uint32_t byte_count, const uint8_t *data) {
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

/* Stores the module brightness applied on the next bklm_show. 0 is off, 255 is full. */
void bklm_set_brightness(uint8_t brightness) {
    bklm_brightness = brightness;
}

/*
 * Wire level a full-scale channel reaches at keyboard maximum and module
 * brightness 255. The per-key LEDs sit at 3.3 V behind frosted acrylic or
 * keycaps; this strip is 5 V and viewed directly, so it needs a heavy derate.
 * Supply accounts for 1/2 of it (a WS2812 at 3.3 V draws about half the
 * current it does at 5 V, and blue/green fall off further) and viewing for
 * another 1/5 (acrylic and keycaps pass about a fifth of the light). The rest
 * is glare: the panel is still harsh to look at the 25 those two alone allow.
 *
 * This doubles as the panel's color resolution, because a channel only ever
 * takes one of BKLM_PEAK_LEVEL + 1 values and that ceiling falls with the
 * keyboard's own brightness. bklm_scale spends what room there is on hue, so
 * what is left over surfaces as saturation instead: #ffe566 reaches the
 * bottom of the range a purer yellow than it really is. Raise this if that
 * matters more than the glare. Nothing lights below about a twelfth
 * brightness, where the peak rounds to zero.
 */
#define BKLM_PEAK_LEVEL 6

/*
 * Level a full-scale channel reaches this frame, rounded before any channel is
 * scaled against it. Peak level first, then the module's own control squared
 * so it tracks perceived brightness (a linear 20% is still most of a WS2812's
 * light), then the keyboard's brightness as the per-key matrix uses it.
 *
 * Q16 internally because BKLM_PEAK_LEVEL is single digits: a chain of integer
 * divides truncates away more than the result is worth, and the level used to
 * reach zero and blank the panel at a third brightness. 65536 * 255 * 255 is
 * the largest intermediate and stays inside uint32_t.
 *
 * Hoisted out of the per-channel path because it costs three software divides.
 */
static uint8_t bklm_frame_level(void) {
    uint32_t level = ((uint32_t)bklm_brightness * bklm_brightness * 65536) / (255 * 255);
    level          = level * BKLM_PEAK_LEVEL;
#    if defined(RGB_MATRIX_ENABLE)
    level = (level * rgb_matrix_get_val()) / RGB_MATRIX_MAXIMUM_BRIGHTNESS;
#    endif
    return (uint8_t)((level + 32768) >> 16);
}

/*
 * Rounds one full-range channel to its share of the frame's peak level.
 *
 * Scaling against the rounded peak, rather than against the exact ratio the
 * brightness chain asks for, is what holds the hue. Every palette entry has a
 * channel at or near 255, so that channel lands exactly on the peak the frame
 * is really using and the other two keep their proportion to it. Rounding each
 * channel independently against the exact ratio let them round apart instead:
 * at a quarter brightness #ffe566 wanted (1.50, 1.35, 0.60), red crossed the
 * half step and green did not, and it went out as (2, 1, 1) -- red, not the
 * (2, 2, 1) yellow the same peak gives here.
 *
 * + 127 rounds exactly: 255 is odd, so a channel never lands on a half step.
 */
static uint8_t bklm_scale(uint8_t component, uint8_t level) {
    return (uint8_t)(((uint32_t)component * level + 127) / 255);
}

/* Visual (x, y) → wire order. Index 0 is bottom-right: even rows (from the
 * bottom) run right to left, odd rows run left to right. */
static uint16_t bklm_index(uint8_t x, uint8_t y) {
    if ((y & 1) == 0) {
        return (uint16_t)y * BKLM_COLS + (BKLM_COLS - 1 - x);
    }
    return (uint16_t)y * BKLM_COLS + x;
}

/* Paints one visual framebuffer and returns after the strip latches. No-op when pixels is NULL. */
void bklm_show(const RGB *pixels) {
    if (pixels == NULL) {
        return;
    }

    const uint8_t level = bklm_frame_level();

    for (uint8_t y = 0; y < BKLM_ROWS; y++) {
        for (uint8_t x = 0; x < BKLM_COLS; x++) {
            const RGB *src = &pixels[(uint16_t)y * BKLM_COLS + x];
            uint8_t   *dst = &bklm_wire[bklm_index(x, y) * 3];
            dst[0]         = bklm_scale(src->g, level);
            dst[1]         = bklm_scale(src->r, level);
            dst[2]         = bklm_scale(src->b, level);
        }
    }

    __asm__ volatile("" ::: "memory");
    __asm__ volatile("cpsid i" ::: "memory");
    bklm_send(1u << LED_MATRIX_MODULE_PIN, LED_MATRIX_MODULE_LED_COUNT * 3, bklm_wire);
    __asm__ volatile("cpsie i" ::: "memory");
    wait_us(280); /* latch / reset */
}

/* Drives the data pin low long enough for the strip to reset. */
void bklm_init(void) {
    gpio_set_pin_output(LED_MATRIX_MODULE_PIN);
    gpio_write_pin_low(LED_MATRIX_MODULE_PIN);
    wait_us(280);
}
