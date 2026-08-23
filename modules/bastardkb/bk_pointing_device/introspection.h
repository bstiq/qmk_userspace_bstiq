#pragma once

/*
    Previously we defined the keycodes in keyboard space.
    We switched to module space, which means the keycodes defined start at a different range.
    For legacy reasons, we manually define the keycodes here, so that we may stay backward compatible.
*/

// IMPORTANT:
// the trigger and toggle keycodes must be one after the other
// if there are keycodes in between, we must respect the odd/even rule.

#define DPI_MOD 0x7E00
#define DPI_RMOD 0x7E01
#define S_D_MOD 0x7E02
#define S_D_RMOD 0x7E03
#define SNIPING 0x7E04
#define SNP_TOG 0x7E05
#define DRGSCRL 0x7E06
#define DRG_TOG 0x7E07
#define CURSOR 0x7E08
#define CUR_TOG 0x7E09
#define PBRIGHT 0x7E0A
#define PBRIGHT_TOG 0x7E0B
#define PZOOM 0x7E0C
#define PZOOM_TOG 0x7E0D
#define PVOLUME 0x7E0E
#define PVOLUME_TOG 0x7E0F
#define PTABS 0x7E10
#define PTABS_TOG 0x7E11
#define PHIST 0x7E12
#define PHIST_TOG 0x7E13
#define INVX 0x7E14
#define INVY 0x7E15