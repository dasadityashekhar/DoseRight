#ifndef HELP_SCREEN_H
#define HELP_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

void help_screen_init(void);
void help_screen_show(void);
void help_screen_set_on_back(void (*cb)(void));

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
