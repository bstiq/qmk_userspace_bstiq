#ifndef SCREEN_BASE_H
#define SCREEN_BASE_H

#include "utilities.h"
#include "lcd.h"

void init_screen_base(void) ;
void load_module_base(void);
bool process_record_screen_base(uint16_t keycode, keyrecord_t *record);

void housekeeping_task_screen_base(void);

#endif