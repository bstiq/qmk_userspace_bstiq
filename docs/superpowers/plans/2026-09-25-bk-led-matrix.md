# LED Matrix Behavioral Plan

> **For agentic workers:** This document is the behavioral contract and naming constraints. Do not implement from it until the isolation design is approved. When implementation starts, use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans task-by-task.

**Goal:** Define what the LED matrix panel shows, and the naming rules every file and function in the module must follow.

**Architecture:** Logic picks which picture and with which content. Display only paints it. Pointer pictograms are one family. Layers, modifiers, typing, and the optional message are the other. Pointer-motion overlays are out of scope.

**Tech Stack:** QMK community module `bk_led_matrix`.

## Global Constraints

- All module source files start with `bk_`.
- All functions start with `bklm_`.
- QMK-mandated names are the only exceptions: `qmk_module.json`, `rules.mk`, `post_config.h`, and lifecycle hooks that QMK derives from the module name (`keyboard_post_init_bk_led_matrix`, `housekeeping_task_bk_led_matrix`, `process_record_bk_led_matrix`).
- This plan describes observable behavior only. No implementation or technical choices.
- Pointer-motion animations (direction bands, linger, overlays on the margin) are out of scope.

---

## What it is

A 12×16 color panel that shows **one** picture of keyboard state at a time. It is a status display, not a second keyboard.

Light level follows the keyboard RGB brightness, and is kept dimmer than the per-key lights so the panel is comfortable to look at directly.

## What it watches

- Highest active layer
- Held modifiers: Shift, Ctrl, Gui, Alt (left and right of a kind are one modifier)
- Recent typing (non-modifier keys)
- Active pointing mode, if a pointing device is present
- An optional explicit message, if something has asked the panel to show text

## Which picture wins

First match, top to bottom:

1. **Pointing mode** — any mode other than Normal
2. **Modifiers** — at least one of Shift / Ctrl / Gui / Alt is held
3. **Message** — an explicit text string has been set
4. **Layer / typing** — default

Leaving a higher-priority state returns to whatever is still true below it.

---

## 1. Pointing mode

Shown only while a non-Normal pointing mode is active.

A pictogram sits in a centered **8×12 well**, with a **2-pixel empty margin** on all sides.

| Mode | Picture |
|---|---|
| Sniping | Cyan reticle |
| Drag-scroll | White scrollbar with a green thumb |
| Cursor | Yellow I-beam |
| Brightness | Yellow sun |
| Zoom | Cyan magnifier, orange handle |
| Volume | Yellow rising bars |
| Tab switch | Overlapping windows (white in front, cyan behind) |
| History | Yellow clock |
| Custom 1–5 | Cyan **C** over a white digit **1–5** |
| Normal | No pictogram. Fall through to modifiers / message / layer. |

---

## 2. Modifiers

Shown while any of the four modifiers is held, and no pointing pictogram is up.

- One row per held modifier, in the order they were pressed
- Labels: `SFT`, `CTL`, `GUI`, `ALT`
- At most **three** rows. A fourth held modifier is not shown
- Color is the **current layer color**. If that layer has no color, white

Releasing a modifier drops its row and the remaining rows keep their order.

---

## 3. Message

Shown when something has asked the panel to display text, and neither a pointing pictogram nor modifiers are up.

- Up to **9** characters in a 3×3 grid
- Left to right, top to bottom
- Letters A–Z and digits 0–9 only (case does not matter). Anything else is a blank cell
- White
- The message stays the fallback until it is replaced

Nothing in the current userspace actually sets a message. It is still in the priority chain.

---

## 4. Layer / typing

Shown when nothing above applies.

**Highest layer is 0.** Typing-speed gauge.

- Left **3 columns** only. The rest of the panel is off
- Bottom row = idle / 0, green
- Top row = 140 WPM, red
- The bar fills upward. Rows between idle and the current speed mix green into red
- A word is 5 keystrokes
- Speed is how many of those words fit in the **last 3 seconds**, so the bar falls back to green once typing stops
- Modifier keys do not count

**Highest layer is 1 or above.** Large layer digit, centered.

- Digits 1–9
- Layers above 9 show as **9**
- Color is that layer’s color. If the layer has no color, white

Layer color is the same color that layer uses on the keyboard underglow.

---

## Out of this plan

Anything that reacts to **pointer motion** (direction bands, linger, overlays on the margin).
