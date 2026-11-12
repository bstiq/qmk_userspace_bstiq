# Pointer movement animations

Date: 2026-09-25

White direction bands overlay the LED matrix for 200 ms after pointing-device movement. The overlay table is empty at first so every mode uses the default band; a later mode-specific animation is one function plus one table slot.

## Behavior

- Any pointing-device movement can trigger a band, on top of whatever is already shown (mode icon, WPM gauge, mods, or layer digit).
- The mode icon (or other frame) stays. The band is painted afterward in the 2 px gutter.
- One band at a time: the dominant axis (`|x|` vs `|y|`). On a tie, keep the live axis if one is already showing, otherwise prefer X.
- Sign mapping (QMK mouse report): `+x` right, `-x` left, `+y` down, `-y` up.
- Color is white (`255, 255, 255`).
- New movement retriggers the 200 ms linger and may change direction. Zero movement does not clear the band early.
- No fade or traveling pixels. On for 200 ms after the last movement, then gone.

## Band geometry

The 12×16 matrix’s outer 2 pixels are the gutter around the 8×12 icon well. `y = 0` is the bottom row.

| Direction | Pixels                          |
|-----------|---------------------------------|
| Right     | `x = 10, 11` (all `y`)          |
| Left      | `x = 0, 1` (all `y`)            |
| Up        | `y = 14, 15` (all `x`)          |
| Down      | `y = 0, 1` (all `x`)            |

Corners belong to that one band.

## Data flow

1. Each pointing report adds `x`/`y` into a signed accumulator (`bkpd_record_mouse_movement`).
2. LED housekeeping is the only consumer. `bkpd_get_mouse_movement` returns the delta since last poll and clears the accumulator.
3. If either axis is non-zero, the LED side picks the dominant direction and (re)starts a 200 ms timer.
4. While the timer is live, after the usual frame it calls `table[mode](dir)`, or the default band if that slot is missing / NULL.

Digitizer Normal and Sniping currently skip `bkpd_process_active_mode`. Those paths must call the same record helper or they never light a band.

## APIs and files

### Pointing module (`bk_pointing_modes`)

- Fix the broken `#ifdef #ifdef` around the accumulator.
- `void bkpd_record_mouse_movement(int16_t x, int16_t y)` — add into the accumulator. Called from `bkpd_process_active_mode` before invert / mode processing, and from the digitizer Normal/Sniping path. Must be in the header; digitizer lives in another file.
- `void bkpd_get_mouse_movement(int16_t *x, int16_t *y)` — consume-on-read, signed. The existing `uint16_t` signature is wrong for direction.

### LED pointer logic (`led_matrix_pointer.c`)

Owns the timer, dominant-axis classification, and the overlay table.

```c
bool led_matrix_pointer_apply_overlay(void);
```

Called after the current frame is composed, before flush. Polls movement, updates or expires the timer, then draws if live. Returns true when the overlay is live or just expired, so housekeeping redraws that tick and does not early-return.

Direction tokens (shared with the display helper):

```c
enum {
    LED_MATRIX_DIR_NONE = 0,
    LED_MATRIX_DIR_RIGHT,
    LED_MATRIX_DIR_LEFT,
    LED_MATRIX_DIR_UP,
    LED_MATRIX_DIR_DOWN,
};
```

Overlay table: indexed by mode id, missing entries are NULL and fall through to `led_matrix_module_fill_band`. The first table may be empty.

### LED display (`led_matrix_module.c`)

```c
void led_matrix_module_fill_band(uint8_t dir);
```

Paints the 2 px gutter white. `led_matrix_module_index` stays private.

Housekeeping still rate-limits at `LED_MATRIX_MODULE_REFRESH_MS` (100 ms). It stays awake while the linger is running and draws one extra frame when the overlay expires. If the pointing header is absent, `led_matrix_pointer_apply_overlay` is a no-op that returns false.

## Out of scope

- Per-mode custom overlay implementations (table is ready; no extra drawers yet).
- Diagonals, fades, or motion along the band.
- Changing linger length or band color at runtime.

## Checks

- Compile a keymap that enables both the pointing and LED matrix modules.
- Getter is `int16_t` and consume-on-read; digitizer Normal/Sniping records movement.
- Housekeeping calls the overlay after the base frame and before flush.
- On device: move each axis in a pointer mode, in Normal (WPM), and with mods held. Band lasts about 200 ms and does not stick.
