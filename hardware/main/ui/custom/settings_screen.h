#ifndef SETTINGS_SCREEN_H
#define SETTINGS_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

void settings_screen_init(void);
void settings_screen_show(void);
void settings_screen_set_on_back(void (*cb)(void));

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
