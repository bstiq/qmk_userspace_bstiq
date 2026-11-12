# bk_split_flash -- post-build hook (Part 8 / T8.2).
#
# QMK includes a community module's post_rules.mk during the build
# (builddefs/build_keyboard.mk:451-456), which is what lets a module add a
# per-build rule. This one emits <TARGET>_combined.uf2: the normal app image plus
# the peer image written at the staging address = __flash_binary_end rounded up to
# a 4096 B sector (the same address the running module computes via
# bksf_get_staging_window()), so a single BOOTSEL drag delivers both the running
# firmware and the peer it distributes.
#
# The peer comes from the raw sidecar (bk_split_flash_peer_image.bin) that
# tools/gen_peer_image.py writes next to the generated blob, so the packaged peer
# is byte-identical to the one embedded in the app.
#
# The step runs only when both the blob and the sidecar are present (a "carrier"
# build that embeds a peer); otherwise it is a no-op. The normal <TARGET>.uf2 is
# never touched -- the combined image is a separate file in the userspace root.

BKSF_MODULE_DIR := $(patsubst %/post_rules.mk,%,$(lastword $(MAKEFILE_LIST)))
BKSF_PEER_C     := $(BKSF_MODULE_DIR)/bk_split_flash_peer_image.c
BKSF_PEER_BIN   := $(BKSF_MODULE_DIR)/bk_split_flash_peer_image.bin

ifneq ($(QMK_USERSPACE),)
ifneq ($(wildcard $(BKSF_PEER_C)),)
ifneq ($(wildcard $(BKSF_PEER_BIN)),)

BKSF_COMBINED_UF2 := $(QMK_USERSPACE)/$(TARGET)_combined.uf2

# NB: the sidecar .bin is NOT listed as a prerequisite -- QMK's generic `%.bin:
# %.elf` pattern rule would try to *build* it. The wildcard gate above already
# ensures it exists, and any change to the peer relinks the app (the blob is
# compiled in), so the uf2/elf prerequisites below already trigger a rebuild.
bksf-combined-uf2: $(BUILD_DIR)/$(TARGET).uf2 $(BUILD_DIR)/$(TARGET).elf
	$(SILENT) || printf "Creating %s\n" "$(notdir $(BKSF_COMBINED_UF2))"
	@python3 $(BKSF_MODULE_DIR)/tools/make_combined_uf2.py \
		--app $(BUILD_DIR)/$(TARGET).uf2 \
		--elf $(BUILD_DIR)/$(TARGET).elf \
		--peer $(BKSF_PEER_BIN) \
		--out $(BKSF_COMBINED_UF2)

all: bksf-combined-uf2

endif
endif
endif
