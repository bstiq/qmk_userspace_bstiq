# bk_split_flash -- post-build hook (Part 8 / T8.2).
#
# This module intentionally emits NO extra UF2 during a plain `qmk compile`.
# The single flashable image is produced by scripts/build_full_uf2.py, which
# calls tools/make_combined_uf2.py after the carrier build to write <TARGET>.uf2
# (the app plus the peer image staged at __flash_binary_end rounded up to a
# 4096-byte sector). Keeping the combination in one place means a build never
# leaves a second _combined.uf2 / _full.uf2 / _peer.uf2 behind: exactly one UF2
# remains for the target.
#
# Previously this hook emitted <TARGET>_combined.uf2 via an `all:` target; that
# rule was removed when the artifact set was consolidated to a single image.
