# Pointer-mode LED matrix logos

Date: 2026-09-25

## Goal

When a non-normal pointer mode is active, the 12×16 RGB LED matrix shows that mode’s full-color logo. Normal mode keeps the existing WPM gauge. This pass is static only: object in the 8×12 well, gutters off. Directional gutter animation comes later.

## Display priority

While `bkpd_mode_get_active_id()` is not `MODE_NORMAL`:

1. Draw that mode’s logo.
2. Do not draw modifier labels, layer digits, pushed text, or the WPM gauge.

When the active mode is `MODE_NORMAL`, keep today’s path: modifiers, then explicit text, then layer 0 WPM gauge or a layer digit.

Sniping held as a modifier on another mode does not get its own logo. Show the other mode. Sniping only changes DPI.

## Layout

Physical matrix: 12 columns × 16 rows. Visual x=0 is left, visual y=0 is top of the drawing. Firmware mapping stays as it is today (`led_matrix_module_index`: x=0 left, y=0 bottom).

- Object well: 8×12, columns 2–9, visual rows 2–13.
- Gutter: 2 pixels on every edge.
  - Top gutter: visual rows 0–1, columns 2–9 (up).
  - Bottom gutter: visual rows 14–15, columns 2–9 (down).
  - Left gutter: columns 0–1, visual rows 2–13 (left).
  - Right gutter: columns 10–11, visual rows 2–13 (right).
  - Four corners stay off. They do not belong to an axis.

## Motion

Deferred. Gutters stay off. Do not sample the pointing device for animation in this pass.

## Palette

Off pixels in a bitmap are `(0, 0, 0)`.

| Token | RGB           | Hex     |
| ----- | ------------- | ------- |
| `c`   | 62, 207, 255  | #3ecfff |
| `w`   | 244, 247, 255 | #f4f7ff |
| `g`   | 61, 255, 154  | #3dff9a |
| `y`   | 255, 229, 102 | #ffe566 |
| `o`   | 255, 138, 61  | #ff8a3d |
| `.`   | 0, 0, 0       | #000000 |

Each logo is 12 strings of 8 characters. Row 0 is the top of the well.

## Logos

### Sniping — cyan reticle

```
..cccc..
.c....c.
c..ww..c
c.w..w.c
c.w..w.c
cw....wc
cw....wc
c.w..w.c
c.w..w.c
c..ww..c
.c....c.
..cccc..
```

### Dragscroll — scrollbar

```
.wwwwww.
.w....w.
.w.gg.w.
.w.gg.w.
.w.gg.w.
.w.gg.w.
.w....w.
.w....w.
.w....w.
.w....w.
.w....w.
.wwwwww.
```

### Cursor — serif I

```
.yyyyyy.
.yyyyyy.
...yy...
...yy...
...yy...
...yy...
...yy...
...yy...
...yy...
...yy...
.yyyyyy.
.yyyyyy.
```

### Brightness — sun disc

```
........
..yyyy..
.yyyyyy.
yyyyyyyy
yyyyyyyy
yyyyyyyy
yyyyyyyy
.yyyyyy.
..yyyy..
........
........
........
```

### Zoom — magnifier

```
.ccccc..
c.....c.
c.....c.
c.....c.
c.....c.
.ccccc..
....oo..
.....oo.
......oo
.......o
........
........
```

### Volume — level bars

```
........
......yy
......yy
....yyyy
....yyyy
..yyyyyy
..yyyyyy
yyyyyyyy
yyyyyyyy
........
........
........
```

### Tab switch — overlapping windows

```
........
.cccccc.
.c....c.
wwwwwww.
w.....c.
w.....c.
w.....c.
w.....c.
wwwwwww.
........
........
........
```

### History — clock

```
........
..yyyy..
.y....y.
y..yy..y
y...y..y
y......y
.y....y.
..yyyy..
........
........
........
........
```

### Custom 1–5 — letter C + digit

Rows 0–5 are the C. Row 6 is blank. Rows 7–11 are the slot digit.

```
..cccc..
.cc..cc.
cc......
cc......
.cc..cc.
..cccc..
........
<digit>
```

Digits (5 rows × 8 columns):

**1**

```
...ww...
..www...
...ww...
...ww...
..wwww..
```

**2**

```
.wwwwww.
......w.
.wwwwww.
.w......
.wwwwww.
```

**3**

```
.wwwwww.
......w.
.wwwwww.
......w.
.wwwwww.
```

**4**

```
.w...w..
.w...w..
.wwwwww.
.....w..
.....w..
```

**5**

```
.wwwwww.
.w......
.wwwwww.
......w.
.wwwwww.
```

Custom 1 uses digit 1, Custom 5 uses digit 5.

Per-key RGB DPI indicators are unchanged. If a non-normal mode is active during a DPI tweak, the LED matrix still shows that mode’s logo.

## Out of scope

- Logo for `MODE_NORMAL`
- Gutter / directional animation (later)
- Animating the object itself
- Unique pictures per custom slot
- Changing per-key RGB, the LCD module, or VIA

## Where it lives

No new QMK module. Split inside `modules/bastardkb/led_matrix/`:

- `led_matrix_icons.h` — palette, `led_matrix_icon_t`, and the bitmap tables. Data only.
- `led_matrix_pointer.c` / `led_matrix_pointer.h` — logic: read `bkpd_mode_get_active_id()` and return the icon for that mode (compose C+digit for custom 1–5).
- `led_matrix_module.c` — display: blit an icon into the well and keep housekeeping/WPM/mods/text.

## Success

Each non-normal mode is recognizable at a glance on the 12×16. Leaving the mode restores the previous matrix behavior.
