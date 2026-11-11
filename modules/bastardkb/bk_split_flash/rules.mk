SRC += bk_split_flash_flash.c
SRC += bk_split_flash_rpc.c
SRC += bk_split_flash_pack.c

# Optional peer image for the T2.5 self-flash test. Generate it with
# tools/gen_peer_image.py; it is then picked up automatically (the module's
# weak defaults are overridden).
BKSF_PEER_IMAGE_C := $(dir $(lastword $(MAKEFILE_LIST)))bk_split_flash_peer_image.c
ifneq ($(wildcard $(BKSF_PEER_IMAGE_C)),)
    SRC += bk_split_flash_peer_image.c
    OPT_DEFS += -DBKSF_PEER_IMAGE_PRESENT
endif
