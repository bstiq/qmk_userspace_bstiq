# Working notes

Standing rules for any task in this repository.

## Build

```bash
wsl -d Ubuntu -- bash -lc "cd /home/quinn/qmk_userspace_private && qmk compile -kb bastardkb/dilemma/4x6_4_procyon -km vendor -c"
```

The module `config.h` is force-included **before** the keymap's `config.h`, so any
keymap override of a module macro needs `#undef` first (otherwise `-Werror`
redefinition).

## Environment and file placement (WSL)

- All work happens in WSL Ubuntu. The agent shell is Windows PowerShell; drive WSL
  with `wsl -d Ubuntu -- bash -lc '<script>'`.
- **Quoting.** The script travels PowerShell → `wsl` → `bash`, so nested quotes get
  mangled: `|`, `$(...)`, parentheses, and inner double quotes in the bash script all
  break (an unbalanced quote makes `bash -lc` hang waiting on stdin). Prefer a
  single-quoted PowerShell string, one simple command per call, and no regex
  alternation; for anything multi-step, write a script file (e.g. under `/home/quinn/`)
  and run that.
- **The two trees.** `QMK_USERSPACE` = `/home/quinn/qmk_userspace_private` (this repo,
  the overlay). `QMK_FIRMWARE` = `/home/quinn/bastardkb-qmk` (**read-only** — never
  edit it).
- **File placement.** This repo holds the *overlay* only: keymap overrides
  (`keyboards/<...>/keymaps/<km>/`), community modules (`modules/`), `docs/`,
  `agents/`, `qmk.json`. The *keyboard definition* — its `keyboard.json` (matrix pins,
  `layouts`), keyboard-level `config.h`, and `*.c` — lives in the **firmware tree** at
  `/home/quinn/bastardkb-qmk/keyboards/...`. Look there for matrix row/col facts and
  bootmagic config.
- **Build artifacts.** `qmk compile` runs make with cwd = `QMK_FIRMWARE`, so
  `BUILD_DIR=.build` resolves to `/home/quinn/bastardkb-qmk/.build/`: the app
  `<TARGET>.{elf,map,uf2}` is **there**, not in the userspace. The `.uf2` is also
  copied to the qmk_firmware root and to `$(QMK_USERSPACE)/<TARGET>.uf2`.

## Git

- **Git is the user's to manage.** Agents do **not** commit, stage, branch, switch,
  push, stash, or run any other mutating git command unless the user explicitly asks.
- Read-only git is fine and encouraged when it helps: `git status`, `git log`,
  `git show`, `git diff` — to understand state or to check whether a change broke
  something (diff the files you touched, or `git log` to find when a line changed).
- Expect commits to appear **mid-task**; the user commits while you work. A clean tree
  or an unexpected new commit is normal, not lost work — confirm with `git status` and
  your files on disk rather than assuming.

## Repository facts

- `.gitignore` covers `*.hex`, `*.bin`, `*.uf2` — but **not** the generated
  `bk_split_flash_peer_image.c`. Do not commit that blob; it is produced per-build by
  `tools/gen_peer_image.py`, and a stale embedded peer garbles the split link.
- The 4 MB flash fix lives in the userspace keymap, e.g.
  `keyboards/bastardkb/charybdis/4x6/keymaps/vendor/config.h`
  (`PICO_FLASH_SIZE_BYTES (4 * 1024 * 1024)`) plus that keymap's `rules.mk`
  (`LDFLAGS += -Wl,--defsym=FLASH_LEN=0x400000`).

## Flash size and EEPROM region (staging math)

- `EXTERNAL_FLASH_SIZE` is the **physical chip** size and is **unrelated** to the
  linker/EEPROM region — it only feeds the external SPI-flash driver
  (`drivers/flash/flash_spi.*`).
- The effective region is `PICO_FLASH_SIZE_BYTES` (RP2040), which defaults to **2 MB**
  unless the keymap overrides it; the linker `FLASH_LEN` follows it, and
  `WEAR_LEVELING_RP2040_FLASH_SIZE` is defined from it.
- As built, the **dilemma 4x6 procyon is a 2 MB region** (its chip is 4 MB but
  `PICO_FLASH_SIZE_BYTES` is not overridden, so the top half is unused): `flash1` =
  `0x200000`, EEPROM base `0x1C0000`, staging window `0x190000`. The charybdis 4x6 is
  a 4 MB region (see the fix above: `flash=0x400000`, eeprom `0x3C0000`).
- Runtime staging bounds come from `bksf_get_staging_window()` (linker symbols), never
  hard-coded; confirm each target from its `.map`.

## Shell and build caveats

- `qmk compile` can exit `0` even when the underlying `gmake` fails (e.g. a failing
  post-build rule); the traceback is in the build log. Check the log and artifact
  presence, not just the exit code.
- Rebuilds are **never byte-identical**: QMK embeds a compile date/time
  (`__DATE__`/`__TIME__`), so two builds of the same source differ by a few bytes in
  `.rodata`. Compare staged vs. embedded at runtime, not two build outputs.
- Do not list a generated `*.bin` as a make prerequisite: QMK's generic
  `%.bin: %.elf` pattern rule will try to *build* it (linking a bogus `.elf`, and
  clobbering the file). Gate on `$(wildcard ...)` and let the app relink instead.

## Packaging (Part 8 / T8.2, combined UF2)

- `modules/bastardkb/bk_split_flash/post_rules.mk` adds a `bksf-combined-uf2` target
  as a prerequisite of `all`. When the peer blob **and** its raw sidecar
  `bk_split_flash_peer_image.bin` are both present, it runs
  `tools/make_combined_uf2.py` to emit `$(QMK_USERSPACE)/<TARGET>_combined.uf2`
  (app at `0x10000000` + peer at `0x10000000 + staging_start`, `staging_start` =
  `__flash_binary_end` read from the ELF `.symtab`, rounded up to 4096 B). It is a
  no-op otherwise, and never touches the normal `<TARGET>.uf2`.
- `tools/gen_peer_image.py` writes the `.c` blob **and** the byte-identical raw
  sidecar `.bin`. The combined step is gated on the `.c`, so a peer build must remove
  the `.c` (a leftover `.bin` alone is not enough to trigger it).

## Guard rails and indicator (Part 9)

- **Image descriptor (T9.1).** `bk_split_flash_image.h` defines `bksf_image_hdr_t`
  (`magic 0xB5F71A6E`, `hdr_version 1`, `proto_version`, `len`, `crc`).
  `gen_peer_image.py` writes `const bksf_image_hdr_t bksf_peer_image_hdr` beside the
  blob; it reads `BKSF_PROTO_VERSION` from `bk_split_flash_proto.h` (so the peer
  stamps the code it was built from) and `--proto-version N` overrides it to
  reproduce an incompatible image. `bksf_validate_image()` (checks magic / descriptor
  version / proto / len / CRC / staging fit) runs **before ARM** in
  `bksf_begin_update` and `bksf_self_update_from_peer_hold`; failure logs
  `bksf: peer image rejected: <reason> -> not armed`.
- **Indicator (T9.2).** `bksf_set_state()`/`bksf_tick_state()` (ERROR auto-clears
  after `BKSF_ERROR_HOLD_MS` 3000 ms); `rgb_matrix_indicators_advanced_bk_split_flash()`
  paints the half per state (white/amber/blue/magenta/red), IDLE leaves the effect
  alone. `bk_split_flash` is last in the keymap `modules` list so it runs after argos
  and overrides it. RGB only — no LED matrix, no OLED (user, 2026-11-12).

## Layout

- Module: `modules/bastardkb/bk_split_flash/` — sources named `bk_split_flash_*`,
  functions named `bksf_*`.
- Plans/design: `docs/superpowers/plans/`, `docs/superpowers/specs/`.
- The QMK firmware checkout at `/home/quinn/bastardkb-qmk` is **read-only** — never
  edit it.

## Hard constraints

- Flash operations run from RAM with interrupts off.
- Erase granularity 4096 B, program granularity 256 B.
- Staging bounds come from linker symbols.
- Fail-safe: a valid boot2 CRC is written only during COMMIT, and COMMIT is the last
  step before reset.

## Workflow

- After each task: log the result in the plan's "Task log" and update its
  "WHERE WE ARE" block.
- The agent does **not** do hardware verification; stop after each task and let the
  user verify on hardware.
- Any change that alters `split_shared_memory` (e.g. `RPC_M2S_BUFFER_SIZE`) requires
  rebuilding **both halves and the peer image** from the same code — a stale peer
  garbles the split link.

## Image generators

- `bash /home/quinn/bksf_gen_images.sh` — the bk_split_flash test image set (rebuilds
  the peer from current code first, then the carriers; restores `config.h`).
- `bash /home/quinn/bksf_t9_images.sh` — the Part 9 set (`bksf_dilemma_T9_{peer,
  slave_update,both_update,bad_peer}.uf2`; `bad_peer` embeds a descriptor with
  `--proto-version 1` to test the guard).
- `bash /home/quinn/bksf_mismatch_images.sh` — the argos red-mismatch test pair.

## QMK build system facts

Settled from the build system (no need to re-investigate):

- **Post-build hook.** A community module's `post_rules.mk` is included by QMK
  (`build_keyboard.mk:451-456`), so a module can add a per-build rule there. The
  userspace root `Makefile` only forwards to QMK and is **not** a post-build hook.
- **Artifacts.** The app `ELF`/`map`/`UF2` are in `$QMK_HOME/.build/<TARGET>.{elf,map,uf2}`
  (= `/home/quinn/bastardkb-qmk/.build/`); the `UF2` is also copied to the userspace
  root.
- **Staging start.** `__flash_binary_end` is a linker symbol available in the
  ELF/map; the staging start is that value rounded **up to a 4096 B sector**. It is
  build-dependent (it moves by ~`0x18000` once a ~96 KB peer is embedded), so read it
  from the `.map` each build rather than reusing a number. Verified example: the
  dilemma T8 carrier (peer embedded) is `__flash_binary_end=0x1002fbe4` → staging
  offset `0x30000`.
- **UF2 writing.** `util/uf2conv.py` can emit multiple UF2 images; `tools/make_combined_uf2.py`
  concatenates the app (at `0x10000000`) and the peer (at `0x10000000 + staging_start`)
  into one file, preserving the app's flags/family.
