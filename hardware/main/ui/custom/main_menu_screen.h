#ifndef MAIN_MENU_SCREEN_H
#define MAIN_MENU_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

typedef enum {
    MAIN_MENU_TAKEN = 0,
    MAIN_MENU_UPCOMING = 1,
    MAIN_MENU_MISSED = 2,
    MAIN_MENU_REFILL = 3,
    MAIN_MENU_CALIBRATE = 4,
    MAIN_MENU_SETTINGS = 5,
    MAIN_MENU_HELP = 6,
    MAIN_MENU_COUNT
} main_menu_item_t;

void main_menu_screen_init(lv_obj_t *parent);
void main_menu_screen_set_on_select(void (*cb)(main_menu_item_t item));
void main_menu_screen_set_on_back(void (*cb)(void));

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
