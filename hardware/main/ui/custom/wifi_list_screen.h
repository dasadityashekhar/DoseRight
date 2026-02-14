#ifndef WIFI_LIST_SCREEN_H
#define WIFI_LIST_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include "lvgl.h"
#include "esp_wifi.h"

void wifi_list_screen_init(void);
void wifi_list_screen_set_ap_records(const wifi_ap_record_t *records, size_t count);
void wifi_list_screen_set_status_text(const char *text);
void wifi_list_screen_set_on_ssid_selected(void (*cb)(const char *ssid));
void wifi_list_screen_set_on_back(void (*cb)(void));

lv_obj_t *wifi_list_screen_get(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
