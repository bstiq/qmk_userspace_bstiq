# Side matrix Argos bar — design spec

**Date:** 2026-09-26  
**Scope:** `modules/bastardkb/bk_led_matrix` only (no changes under `argos/`).

## Goal

Mirror Argos underglow configuration for LED indices **0–15** onto the **leftmost column** (x = 0) of the 12×16 side LED matrix, and show the active layer digit one column to the right with layer color from Argos when on layers 1–9.

## Requirements (decisions)

| Topic | Decision |
|-------|----------|
| When to update cache | **B:** Only on RGB matrix indicator passes whose `[led_min, led_max)` overlaps `[0, 15)`; update only indices in the intersection. |
| Row mapping | Index **i** → framebuffer **y = 15 − i** (index 0 at top). Column **x = 0**. |
| Layer 0 | Per-index colors from Argos layer 0 entries 0–15 (same rules as `rgb_matrix_indicators_advanced_argos`: `custom`, `on`, `passthrough`, brightness scaling, clamp). |
| Layer ≥ 1 | Solid bar: all updated indices use `argos_rgb_get_layer_color(active_layer)`. |
| Layer digit | With Argos enabled and layer ≥ 1: 4×6 digit at **x = 1** (was x = 0). Without Argos: no bar; digit stays at **x = 0** when layer ≥ 1. |
| Caps / pointing | Same intent as Argos (**A**), but via two **stub** functions in bk_led_matrix (return false / no-op until implemented later). |
| Underglow | This hook does **not** call `rgb_matrix_set_color`; always `return true` so the Argos underglow chain is unchanged. |
| Argos disabled | `rgb_matrix_indicators_advanced_bk_led_matrix` no-op; no column draw; layer digit at x = 0. |

## Architecture

1. **`rgb_matrix_indicators_advanced_bk_led_matrix`** (new `led_matrix_indicators.c`):  
   - Guard: `#ifdef COMMUNITY_MODULE_ARGOS_ENABLE`.  
   - Early exit if `bklm_rgb_indicators_should_defer()`.  
   - If `bklm_rgb_indicators_caps_lock_override(&rgb)` fills override color, write that into cache slots for indices in the current slice (when stub is implemented; stub returns false for now).  
   - Compute overlap `[start, end)` between `[led_min, led_max)` and `[0, 16)`.  
   - For each `i` in overlap, set `bklm_argos_bar_cache[i]`.

2. **`bklm_draw_argos_bar_column(RGB *pixels)`** (same file or small helper):  
   - If Argos disabled, return.  
   - Blit cache to `pixels[y * BKLM_COLS + 0]` with `y = 15 - i`.

3. **`housekeeping_task_bk_led_matrix`**: After mods, call `bklm_draw_argos_bar_column`, then `bklm_layers_paint`. Pointer mode unchanged (full well override).

4. **`bklm_layers_blit` / `bklm_layers_paint`**: Parameterize horizontal origin: **x = 1** when Argos + layer ≥ 1, else **x = 0**.

## Stubs (implement later)

```c
bool bklm_rgb_indicators_should_defer(void);
bool bklm_rgb_indicators_caps_lock_override(RGB *out);
```

- **`should_defer`:** Later: pointing DPI change / special RGB modes (mirror `bkpd_is_changing_dpi_settings` / `bkpd_mode_should_handle_rgb`). **Now:** always `false`.  
- **`caps_lock_override`:** Later: if caps on, set `*out` to brightness-scaled red and return `true`. **Now:** always `false`.

## Shared Argos color math

Private helper in bk_led_matrix (copy logic from `argos_rgb.c`, do not modify argos):

- Scale stored RGB by `rgb_matrix_get_val() / RGB_MATRIX_MAXIMUM_BRIGHTNESS`.  
- Clamp channels to `RGB_MATRIX_MAXIMUM_BRIGHTNESS - 40`.  
- Layer 0: use `argos_rgb_get_led_at_position(&entry, 0, i, 0)` and respect `custom` / `on` / `passthrough` (off → black; passthrough → leave cache unchanged or black — match argos: only set when not passthrough).

## Frame composition order

1. Clear framebuffer  
2. Pointer icon **or** (modifier icons → Argos column → layer digit)  
3. `bklm_show`

## Out of scope

- Changes to `modules/bastardkb/argos/**`  
- Implementing pointing/caps defer stubs (signatures only)  
- Host / VIA sync for the side matrix

## Verification

- Firmware build with Argos + bk_led_matrix enabled.  
- Layer 0: column reflects Argos indices 0–15.  
- Layer 1+: solid layer color column + digit at x = 1.  
- Build without Argos: no regression on digit position (x = 0).
