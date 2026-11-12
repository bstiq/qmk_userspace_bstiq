#pragma once

/* Split serial link speed, applied to every `-km vendor` build in this userspace
 * (USER_NAME defaults to the keymap name). The RP2040 PIO split link is wire-bound,
 * so the baud is the hard ceiling for bk_split_flash. 625000 is measured reliable
 * and gives an exact PIO clock divider (125e6 / (8 * 625000) = 25.0), so no divider
 * dither. 921600 (div 16.95, dithered) was too fast for this cable; 460800 is the
 * documented-safe fallback.
 *
 * Set this globally here rather than per keymap. A single keymap can still override
 * it (its config.h is included after this one, e.g. the bksf test-image scripts). */
#define SERIAL_USART_SPEED 625000
