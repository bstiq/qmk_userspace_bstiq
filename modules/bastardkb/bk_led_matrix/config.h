// Copyright 2026 @ykz89
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Local copy of bstiq/qmk_userspace_bstiq branch ledmatrix (b640cd6),
// modules/bastardkb/bk_led_matrix, patched for the Dilemma trackball with
// argos + bk_pointing_device. Changes are marked "ykz89:".

// ykz89: the panel half learns the pointer mode over its own split RPC
// (bk_led_matrix.c) instead of a patched bk_pointing_device. Keep argos's RPC.
#ifdef COMMUNITY_MODULE_ARGOS_ENABLE
#    undef SPLIT_TRANSACTION_IDS_KB
#    define SPLIT_TRANSACTION_IDS_KB RPC_ID_RGB_SYNC, RPC_ID_BKLM_POINTER_SYNC
#else
#    define SPLIT_TRANSACTION_IDS_KB RPC_ID_BKLM_POINTER_SYNC
#endif

// ykz89: the held-modifier names are drawn on the panel half, which is not the
// USB half when the cable is on the right.
#ifndef SPLIT_MODS_ENABLE
#    define SPLIT_MODS_ENABLE
#endif
