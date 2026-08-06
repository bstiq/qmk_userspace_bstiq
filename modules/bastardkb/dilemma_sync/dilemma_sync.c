#include QMK_KEYBOARD_H
#include "dilemma_sync.h"
#include "transactions.h"

dilemma_status_t dilemma_status = { 0 };
dilemma_status_t dilemma_status_prev = { 0 };

void update_dilemma_status(void);
void dilemma_sync_handler(uint8_t initiator2target_buffer_size, const void* initiator2target_buffer, uint8_t target2initiator_buffer_size, void* target2initiator_buffer);

void keyboard_post_init_dilemma_sync(void) {
    transaction_register_rpc(RPC_ID_DILEMMA_SYNC, dilemma_sync_handler);
}

void housekeeping_task_dilemma_sync(void) {
    if (is_keyboard_master()) {
        update_dilemma_status();
        // TODO retry sync if it fails
        if(memcmp(&dilemma_status, &dilemma_status_prev, sizeof(dilemma_status_t))) {
            transaction_rpc_send(RPC_ID_DILEMMA_SYNC, sizeof(dilemma_status), &dilemma_status);
            dilemma_status_prev = dilemma_status;
        }
    }
}

void dilemma_sync_handler(uint8_t initiator2target_buffer_size, const void* initiator2target_buffer, uint8_t target2initiator_buffer_size, void* target2initiator_buffer) {
    if (!is_keyboard_master()) {
        if (initiator2target_buffer_size == sizeof(dilemma_status_t)) {
            dilemma_status = *(const dilemma_status_t*)initiator2target_buffer;
        }
    }
}

void update_dilemma_status(void) {
    dilemma_status.mods = get_mods();
    dilemma_status.layer = get_highest_layer(layer_state);
    dilemma_status.sniping = dilemma_get_pointer_sniping_enabled();
    dilemma_status.dpi = dilemma_get_pointer_default_dpi();
    dilemma_status.s_dpi = dilemma_get_pointer_sniping_dpi();
    dilemma_status.scrolling = dilemma_get_pointer_dragscroll_enabled();
    dilemma_status.rgb_enabled = rgb_matrix_is_enabled();
    dilemma_status.rgb_effect_mode = rgb_matrix_get_mode();
    dilemma_status.rgb_val = rgb_matrix_get_val();
}

const dilemma_status_t get_dilemma_status(void) {
    return (const dilemma_status_t)dilemma_status;
}