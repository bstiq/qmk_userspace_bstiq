# bk_split_flash — handoff prompt: start Part 8 (2026-11-12)

Resume the split-flash work. Read `agents/` first (this folder is the always-read
agent context; `AGENTS.md` at the repo root points here), then:

- `docs/superpowers/plans/2026-11-11-split-flash-both-halves-parts.md` — task
  breakdown + source of truth. Read its "WHERE WE ARE", "Local build environment",
  **Part 8**, and the **"Part 8 recon — resolved decisions"** block.
- `docs/superpowers/plans/2026-11-10-split-flash-both-halves.md` — the design.
- `agents/working-notes.md` — build command, constraints, and the settled QMK
  build-system facts.

State: Parts 1–7 done and HW-verified (T0–T4.3, T5.1/T5.2, T7.1/T7.2 on the dilemma
4x6 procyon). Part 6 (host flashing) is deferred and not started — leave it alone.

Do **Part 8 — Combined-UF2 packaging**. Only Part 8; do not start Parts 9/10. The open
design questions are already decided in the plan's "Part 8 recon — resolved
decisions"; follow them, don't re-open them.

- **T8.1 — staging write-back + verification.** Confirm the ROM mass-storage writer
  accepts a UF2 block targeting the staging address, and add a `BKSF_TEST_STAGING`
  selector that reads the staging region and compares it to the embedded peer (size +
  CRC), logging match/mismatch. Verify by dragging a combined UF2 to `RPI-RP2` and
  running the selector.
- **T8.2 — per-build combined UF2.** Add a post-build step (a community module's
  `post_rules.mk` is the hook; the userspace root `Makefile` is not) that emits
  `<TARGET>_combined.uf2`: app at `0x10000000` + peer at
  `0x10000000 + staging_start`, where `staging_start` is `__flash_binary_end` rounded
  up to 4096 B. The peer comes from a raw sidecar `.bin` that `gen_peer_image.py`
  writes alongside the generated C blob (so the packaged peer is byte-identical to
  the embedded one). Run only when the peer is present; never overwrite the normal
  `<TARGET>.uf2`.

Constraints: module at `modules/bastardkb/bk_split_flash/` (`bk_split_flash_*`
sources, `bksf_*` functions); never edit `/home/quinn/bastardkb-qmk`. Flash ops run
from RAM with IRQs off; 4096 B erase / 256 B program granularity; staging bounds from
linker symbols. Fail-safe: a valid boot2 CRC is written only during COMMIT, and COMMIT
is the last step before reset.

Build: `wsl -d Ubuntu -- bash -lc "cd /home/quinn/qmk_userspace_private && qmk compile -kb bastardkb/dilemma/4x6_4_procyon -km vendor -c"`
(the module `config.h` is force-included before the keymap's, so overrides need
`#undef` first).

Split-memory note: any change that alters `split_shared_memory` requires rebuilding
both halves **and the peer image** from the same code — a stale peer garbles the link.

You don't do hardware verification; the user does.

After each task: log results in the plan's "Task log" and update "WHERE WE ARE";
stop for the user's HW verification.
