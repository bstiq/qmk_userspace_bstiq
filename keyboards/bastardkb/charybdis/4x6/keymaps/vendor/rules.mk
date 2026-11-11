VIA_ENABLE = yes

# The RP2040 QSPI flash is 4 MB, but the linker region defaults to 2 MB
# (`FLASH_LEN` undefined -> 2048k in RP2040_FLASH_TIMECRIT.ld). Match the real
# chip so the whole build agrees on the flash size.
LDFLAGS += -Wl,--defsym=FLASH_LEN=0x400000
