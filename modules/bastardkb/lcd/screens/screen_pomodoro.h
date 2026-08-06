#ifndef SCREEN_POMODORO_H
#define SCREEN_POMODORO_H

void init_screen_pomodoro(void) ;
void refresh_screen_pomodoro(void);
bool process_record_screen_pomodoro(uint16_t keycode, keyrecord_t *record);
void load_module_pomodoro(void);
void housekeeping_task_screen_pomodoro(void);

#endif