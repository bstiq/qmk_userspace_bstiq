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
static uint8_t  argos_led_module_color[ARGOS_LED_MODULE_LED_COUNT * 3];
static uint8_t  argos_led_module_wire[ARGOS_LED_MODULE_LED_COUNT * 3];
static uint8_t  argos_led_module_brightness = 255;
static uint32_t argos_led_module_rng        = 0xA5A5u;

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

static uint8_t argos_led_module_scale(uint8_t component) {
    /* Square the control so it tracks perceived brightness. A linear 20%
     * (51/255) is still most of a WS2812's light. */
    uint8_t level = ((uint16_t)argos_led_module_brightness * argos_led_module_brightness) / 255;
    return ((uint16_t)component * level) / 255;
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

static uint8_t argos_led_module_random8(void) {
    argos_led_module_rng ^= argos_led_module_rng << 13;
    argos_led_module_rng ^= argos_led_module_rng >> 17;
    argos_led_module_rng ^= argos_led_module_rng << 5;
    return (uint8_t)argos_led_module_rng;
}

static void argos_led_module_test_random(void) {
    argos_led_module_set_brightness((255 * 20) / 100);
    for (uint16_t i = 0; i < ARGOS_LED_MODULE_LED_COUNT; i++) {
        argos_led_module_set_color(i, argos_led_module_random8(), argos_led_module_random8(), argos_led_module_random8());
    }
}

void argos_led_module_init(void) {
    gpio_set_pin_output(ARGOS_LED_MODULE_PIN);
    gpio_write_pin_low(ARGOS_LED_MODULE_PIN);
    wait_us(280); /* reset the strip before the first frame */
    argos_led_module_rng ^= timer_read32() | 1u;
}

void argos_led_module_task(void) {
    static uint32_t last_update = 0;

    if (timer_elapsed32(last_update) >= ARGOS_LED_MODULE_REFRESH_MS) {
        last_update = timer_read32();
        argos_led_module_test_random();
        argos_led_module_flush();
    }
}

#endif
