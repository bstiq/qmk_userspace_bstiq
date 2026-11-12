# Bobbing duck on the base layer

Design for a rubber-duck scene on the `bk_led_matrix` 12×16 well, shown only
on layer 0.

## Goal

Layer 0 currently leaves the well dark except for modifier tiles —
`bklm_layers_paint` returns immediately when the highest layer is 0. Fill that
space with a rubber duck floating on a thin band of water, bobbing one pixel up
and down.

## The scene

The sprite is 11 wide × 13 tall. Eleven columns cannot be centered in twelve,
so the spare column goes on the left, behind the beak, rather than behind the
tail. The duck has two vertical positions one row apart; the hull is drawn over
the water, so bobbing changes how deep the duck sits rather than sliding the
whole picture.

The waterline sits at the row where the body flares out. A shallower band
leaves a wedge of dark cells on the low-left, where the hull's curve pulls away
from a flat surface.

```
low position                     high position
. . . . . . . . . . . .          . . . . . . . . . . . .
. . . . . . . . . . . .          . . . . . # # # . . . .
. . . . . # # # . . . .          . . . . # O O O # . . .
. . . . # O O O # . . .          . . . # O O O O O # . .
. . . # O O O O O # . .          . . # O O # O O O # . .
. . # O O # O O O # . .          . * * O O O O O O # . .
. * * O O O O O O # . .          . . # O O O O O O # . .
. . # O O O O O O # . .          . . . # O O O O # . . .
. . . # O O O O # . . .          . . . . # O O # . . . .
. . . . # O O # . . . .          ~ ~ ~ # O O O O # # # #
~ ~ ~ # O O O O # # # #          ~ ~ # O O O O O O O O #
~ ~ # O O O O O O O O #          ~ # O O O O O O O O O #
~ # O O O O O O O O O #          ~ # O O O O O O O O # ~
~ # O O O O O O O O # ~          ~ ~ # # # # # # # # ~ ~
~ ~ # # # # # # # # ~ ~          ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~
~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~          ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~
```

Two deliberate departures from the reference picture:

- The beak is two orange cells butted directly against the body. In the
  reference a black cell separates beak from head, which works over cyan water
  but would leave the beak floating detached over the dark well.
- `.` is transparent, not black. Water shows through around the hull instead of
  being punched out of it.

The black outline needs no special casing for the two backgrounds. Over water it
reads as an edge; over the dark well it vanishes, which is exactly right,
because the dark well already separates the yellow body from its surroundings.

Colors are the values already in `led_matrix_icon_palette`: `#ffe566` body,
`#ff8a3d` beak, `#3ecfff` water, black outline.

## Sprite encoding

Char-art rows (`"....###...."`) in `led_matrix_duck.c`, mapped to colors by a
local switch.

Neither existing style in the module fits. The `PIX_*` tables in
`led_matrix_data.h` cost 143 tokens across thirteen ~300-character lines for a
sprite this size, and including that header means either duplicating the whole
pointer icon table in flash or splitting the palette behind a second guard.
`led_matrix_mods.c` packs one bitmask per row with a `.###.` comment alongside,
which only works for single-color glyphs.

The `bk-led-matrix` rule asks that bitmaps stay readable. With char art the
bitmap *is* the readable form, so there is no parallel comment to drift out of
sync. Colors live in the duck file as RGB literals with a note that they match
the shared palette — the same thing `led_matrix_mods.c` already does.

Packing two bits per pixel into a `uint32_t` per row would cost 52 bytes instead
of ~150, but it cannot be read or hand-edited. Not a trade worth making on an
RP2040.

## Code

New `led_matrix_duck.{c,h}` beside the other feature files, added to `rules.mk`.

One entry point, `bklm_draw_bobbing_duck(RGB *pixels)`. It self-gates like the
other painters: it returns early when `pixels` is NULL, when the highest layer
is not 0, or when any modifier is held. Hiding on a held modifier takes the
water with it, so modifier tiles get a clean dark well.

The bob carries no state. `timer_read32()` is milliseconds since boot, so
`(timer_read32() / 2000) & 1` toggles every two seconds with no static variable
and no frame counting. The phase jumps once per counter wrap, roughly every 49
days, which is invisible.

A `_Static_assert` pins the sprite inside the panel. The y-flip computes
`y = (BKLM_ROWS - 1) - row`, so a sprite that grew past the bottom would
underflow `y` and write outside the framebuffer.

In `bk_led_matrix.c` the duck becomes the layer-0 backdrop, drawn before the
modifier tiles and the layer digit and inside the existing pointer-mode branch
so pointer icons keep owning the whole well:

```c
memset(frame, 0, sizeof(frame));
if (!bklm_pointer_paint(frame)) {
    bklm_draw_bobbing_duck(frame);
    bklm_draw_active_modifier_icons(frame);
    bklm_layers_paint(frame);
}
bklm_show(frame);
```

The frame-composition list in `.cursor/rules/bk-led-matrix.mdc` enumerates the
draw order and is updated to match.

## Out of scope

Animated water, a horizontally travelling duck, and any duck on layers 1–9.
