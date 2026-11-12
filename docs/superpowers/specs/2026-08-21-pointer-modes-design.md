# Pointer modes: hold/toggle pairs without growing switches

Date: 2026-08-21

## Problem

Drag-scroll, sniping, and cursor-as-arrows each need a hold keycode and a toggle keycode. Today that is duplicated in `process_record`, `is_mouse_record_kb`, CPI selection, and `pointing_device_task`. Adding RGB or brightness as more pairs would copy the same control flow again.

Sniping does not consume motion (CPI only). Drag-scroll and cursor consume `x`/`y`. Those must share one dispatch path anyway.

## Goals

- One decode path for every hold/toggle pair.
- One exclusive effective mode: latched toggle, momentary overlay, idle = normal cursor.
- Motion handling via a table of `{get_cpi, apply}`. No `on_enter` / `on_exit`.
- Shared `buf_x` / `buf_y` owned by the dispatcher, passed into `apply` as pointers, zeroed when effective mode changes.
- Existing VIA/keymap values `0x7E04`–`0x7E08` stay put.

## Non-goals

- LCD status or `lcd.c` changes.
- Argos UI for the new encoded range (firmware accepts the keycodes; configurator names can come later).
- RGB or brightness `apply` bodies (the table must make those a new row later, not a new switch).
- Two-deep hold stack.
- Persisting latch/overlay across reboot.
- Changing DPI step keycodes (`DPI_MOD`, `DPI_RMOD`, `S_D_MOD`, `S_D_RMOD`).

## Decision

Encoded keycodes (`BKPD_MO(id)` / `BKPD_TG(id)`) plus a const descriptor table, with a frozen compat table for the old integers.

Rejected: scanning `{hold_kc, tog_kc}` rows as the public API (keycodes are not self-describing). Rejected: X-macros (no second representation to keep in sync yet).

## Keycodes

Frozen aliases in `introspection.h` (do not renumber):

| Keycode  | Value  | Mode   | Kind   |
|----------|--------|--------|--------|
| SNIPING  | 0x7E04 | SNIPE  | hold   |
| SNP_TOG  | 0x7E05 | SNIPE  | toggle |
| DRGSCRL  | 0x7E06 | SCROLL | hold   |
| DRG_TOG  | 0x7E07 | SCROLL | toggle |
| CURSOR   | 0x7E08 | CURSOR | hold   |
| CUR_TOG  | 0x7E09 | CURSOR | toggle |

`CUR_TOG` is new. `CURSOR` already exists as hold-only.

Encoded range, starting after that block:

```c
#define BKPD_QK_MODE 0x7E10
#define BKPD_MO(id)  (BKPD_QK_MODE + ((id) << 1))
#define BKPD_TG(id)  (BKPD_QK_MODE + ((id) << 1) + 1)
```

`id` is the mode enum value. Bit 0 is kind (0 = hold, 1 = toggle).

Decode:

1. Look up the six frozen aliases.
2. Else if `keycode` is in `[BKPD_QK_MODE, BKPD_QK_MODE + 2 * BKPD_MODE_COUNT)`, extract `id` and kind.
3. Else not a mode key.

If `id >= BKPD_MODE_COUNT`, ignore the event (do not index the table).

`BKPD_MO(SNIPE)` is a second, valid way to hold-snipe. Old maps keep using `SNIPING`.

## Mode table

```c
typedef enum {
    BKPD_MODE_SNIPE = 0,
    BKPD_MODE_SCROLL,
    BKPD_MODE_CURSOR,
    BKPD_MODE_COUNT
} bkpd_mode_id_t;

#define BKPD_MODE_NONE 0xFF

typedef report_mouse_t (*bkpd_apply_fn)(report_mouse_t report, int16_t *buf_x, int16_t *buf_y);

typedef struct {
    uint16_t (*get_cpi)(void); /* NULL → default DPI */
    bkpd_apply_fn apply;       /* NULL → passthrough */
} bkpd_mode_desc_t;
```

First-pass rows:

| id     | get_cpi                         | apply                                      |
|--------|---------------------------------|--------------------------------------------|
| SNIPE  | sniping DPI getter              | NULL                                       |
| SCROLL | drag-scroll DPI (100 today)     | invert + threshold → `h`/`v`               |
| CURSOR | NULL                            | threshold → arrow taps, consume `x`/`y`    |

Idle (`BKPD_MODE_NONE`) is not a row: default DPI, passthrough.

Digitizer drag-scroll threshold stays inside the scroll `apply` (`BK_POINTING_DEVICE_DRAGSCROLL_BUFFER_SIZE_DIGITIZER` when that driver is on). Cursor tap implementation stays `argos_keycode_tap` as today.

A later RGB or brightness mode is a new enum value, a table row, and `BKPD_MO`/`BKPD_TG` — no change to `process_record` or the task.

## State

RAM only (not EEPROM):

- `latched`: `BKPD_MODE_NONE` or a mode id, from toggles.
- `momentary`: `BKPD_MODE_NONE` or a mode id, from holds.
- `effective = (momentary != NONE) ? momentary : latched`

EEPROM bits `is_dragscroll_enabled`, `is_sniping_enabled`, `is_cursor_enabled` are not the runtime source of truth. Keep the union layout. On EEPROM read, continue forcing the old flags false; start `latched` and `momentary` at `NONE`.

Existing getters return `effective ==` that mode. Existing setters assign `latched` (enable → that id, disable → `NONE` if `latched` was that id) and do not clear `momentary`. Last setter wins, exclusive.

## Hold / toggle

`process_record` updates `latched` / `momentary` directly. It must not call `bkpd_set_pointer_*_enabled(record->event.pressed)` — that is what makes a hold cancel a latch today.

On a decoded hold:

- Press: `momentary = id` (last press wins if two holds are down).
- Release: if `momentary == id`, set `momentary = NONE`. Releasing a hold that does not own `momentary` is a no-op.

On a decoded toggle, press only:

- `latched = (latched == id) ? NONE : id`

Intentional behavior change: holding a mode that is already latched, then releasing, leaves the latch on. Today `DRGSCRL` up forces drag-scroll off.

## Dispatcher

Key event: decode → update latched/momentary → if `effective` changed, `buf_x = buf_y = 0` and `bkpd_maybe_update_cpi()`.

CPI: `pointing_device_set_cpi(get_cpi())` where `get_cpi` is `table[effective].get_cpi` if `effective != NONE` and non-NULL, else the default DPI getter. No if-chain on named modes.

Mouse report (master only, as today):

- If `effective == NONE` or `apply == NULL`: leave the report (sniping / idle).
- Else: `report = apply(report, &buf_x, &buf_y)`.

Then `pointing_device_task_user(report)`.

`apply` accumulates into the pointed-to buffers, may zero `report.x`/`y`, and may emit `h`/`v` or side effects (arrow taps). Axis invert for drag-scroll lives in the scroll `apply`.

`is_mouse_record_kb`: true if decode succeeds (including `CURSOR` / `CUR_TOG` and the encoded range). DPI step keys stay listed separately.

## Files

Primary: `modules/bastardkb/bk_pointing_device/bk_pointing_device.c`, `.h`, `introspection.h`.

Do not edit `lcd.c`. Do not add Argos commands for “current mode.”

## Checks

- Hold overlays a different latched mode; release restores the latch.
- Hold of the already-latched mode does not clear on release.
- Toggle off with no hold returns to idle cursor.
- Two holds: last press wins; inactive release is a no-op.
- Sniping only changes CPI; motion still moves the pointer.
- Drag-scroll still emits `h`/`v` with invert and digitizer threshold.
- Cursor still emits arrow taps and consumes motion; `CUR_TOG` latches it.
- Re-entering a consume mode does not fire a leftover buffer tick.
- `DPI_MOD` / `S_D_MOD` still cycle DPI.
- Keymaps using `0x7E04`–`0x7E08` still decode.
- Invalid encoded `id` does nothing.
- Auto-mouse: mode keys, including `CURSOR`, count as mouse records.
