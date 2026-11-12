# bk_split_flash — handoff prompt: start Part 10 (2026-11-12)

Resume the split-flash work. Read `agents/` first (this folder is the always-read
agent context; `AGENTS.md` at the repo root points here), then:

- `docs/superpowers/plans/2026-11-11-split-flash-both-halves-parts.md` — task
  breakdown + source of truth. Read its "WHERE WE ARE", "Local build environment",
  and the Part you are starting.
- `docs/superpowers/plans/2026-11-10-split-flash-both-halves.md` — the design.
- `agents/working-notes.md` — build command, constraints, and the settled QMK
  build-system facts.

State: Parts 1–7 done and HW-verified (T0–T4.3, T5.1/T5.2, T7.1/T7.2 on the dilemma
4x6 procyon). Part 6 (host flashing) is deferred and not started — leave it alone.
**Parts 8 (combined UF2) and 9 (guard rails + recovery) are code complete and pass
offline checks; HW verification is pending** — the user runs it. Part 9 added an
image version/CRC guard (T9.1), an RGB "flash pending" indicator (T9.2), and a
documented recovery procedure (T9.3); see the plan's "Part 9 bring-up".

Next: **Part 10 — Bootmagic trigger** (start the update by holding a key at
power-on), *after* the user HW-verifies Parts 8/9. Only the current part; do not
start later parts. Follow the plan's task steps; don't re-open settled decisions.

Constraints: module at `modules/bastardkb/bk_split_flash/` (`bk_split_flash_*`
sources, `bksf_*` functions; the new T9.1 descriptor lives in
`bk_split_flash_image.h`); never edit `/home/quinn/bastardkb-qmk`. Flash ops run
from RAM with IRQs off; 4096 B erase / 256 B program granularity; staging bounds from
linker symbols. Fail-safe: a valid boot2 CRC is written only during COMMIT, and COMMIT
is the last step before reset.

Build: `wsl -d Ubuntu -- bash -lc "cd /home/quinn/qmk_userspace_private && qmk compile -kb bastardkb/dilemma/4x6_4_procyon -km vendor -c"`
(the module `config.h` is force-included before the keymap's, so overrides need
`#undef` first).

Test images: `bash /home/quinn/bksf_t9_images.sh` rebuilds the Part 9 set (peer from
current code, then the carriers; restores `config.h`). The Part 8 set is
`bksf_images/bksf_dilemma_T8_*`; Part 7 is `bash /home/quinn/bksf_gen_images.sh`.

Split-memory note: any change that alters `split_shared_memory` requires rebuilding
both halves **and the peer image** from the same code — a stale peer garbles the link.

You don't do hardware verification; the user does.

After each task: log results in the plan's "Task log" and update "WHERE WE ARE";
stop for the user's HW verification.
