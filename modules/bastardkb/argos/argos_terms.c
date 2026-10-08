/*
 * Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Publicw License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

bool argos_dispatch_command_terms(uint8_t *command_id, uint8_t *command_data){
    if (*command_id != argos_id_term_config)
        return true; // do not process further

    command_id = &(command_data[0]);
    command_data = &(command_data[1]);
    switch (*command_id) {
        case argos_id_term_flow_tap_get: {
            send_data = true;
            command_data[0] = (argos_config.flow_tap_term >> 8) & 0xFF;
            command_data[1] = argos_config.flow_tap_term & 0xFF;
            break;
        }
        case argos_id_term_flow_tap_set: {
            argos_config.flow_tap_term = (command_data[0] << 8) | command_data[1];
            printf("flow tap: %d\n", argos_config.flow_tap_term);
            argos_write_eeprom(ARGOS_OFFSET_CONFIG, &argos_config,
                               sizeof(argos_config));
            send_data = true;
            break;
        }
        default:
            return false;

    }
    
    return true;
}

uint16_t get_flow_tap_term(uint16_t keycode, keyrecord_t* record,
    uint16_t prev_keycode) {
    if (is_flow_tap_key(keycode) && is_flow_tap_key(prev_keycode)) {
        return argos_config.flow_tap_term;
    }
    return 0;
}