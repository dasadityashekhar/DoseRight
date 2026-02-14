#include "wifi_list_screen.h"

#include <string.h>

#define WIFI_LIST_MAX_APS 20

static lv_obj_t *wifi_list_screen = NULL;
static lv_obj_t *wifi_list_title = NULL;
static lv_obj_t *wifi_list_status = NULL;
static lv_obj_t *wifi_list = NULL;
static lv_obj_t *wifi_list_back_btn = NULL;

static void (*ssid_selected_cb)(const char *ssid) = NULL;
static void (*back_cb)(void) = NULL;

static char ssid_storage[WIFI_LIST_MAX_APS][33];

static void on_back_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    if (back_cb) {
        back_cb();
    }
}

static void on_ssid_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    const char *ssid = (const char *)lv_event_get_user_data(e);
    if (ssid_selected_cb && ssid && ssid[0] != '\0') {
        ssid_selected_cb(ssid);
    }
}

void wifi_list_screen_init(void)
{
    if (wifi_list_screen) {
        return;
    }

    wifi_list_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(wifi_list_screen, LV_OBJ_FLAG_SCROLLABLE);

    wifi_list_title = lv_label_create(wifi_list_screen);
    lv_label_set_text(wifi_list_title, "Select WiFi");
    lv_obj_align(wifi_list_title, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_text_font(wifi_list_title, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

    wifi_list_status = lv_label_create(wifi_list_screen);
    lv_label_set_text(wifi_list_status, "Scanning...");
    lv_obj_align(wifi_list_status, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_set_style_text_font(wifi_list_status, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);

    wifi_list_back_btn = lv_btn_create(wifi_list_screen);
    lv_obj_set_size(wifi_list_back_btn, 70, 26);
    lv_obj_align(wifi_list_back_btn, LV_ALIGN_TOP_LEFT, 8, 8);
    lv_obj_add_event_cb(wifi_list_back_btn, on_back_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_lbl = lv_label_create(wifi_list_back_btn);
    lv_label_set_text(back_lbl, "Back");
    lv_obj_center(back_lbl);

    wifi_list = lv_list_create(wifi_list_screen);
    lv_obj_set_size(wifi_list, 300, 170);
    lv_obj_align(wifi_list, LV_ALIGN_BOTTOM_MID, 0, -8);
}

void wifi_list_screen_set_ap_records(const wifi_ap_record_t *records, size_t count)
{
    if (!wifi_list) {
        return;
    }

    lv_obj_clean(wifi_list);

    if (!records || count == 0) {
        lv_list_add_text(wifi_list, "No networks found");
        return;
    }

    size_t shown = count;
    if (shown > WIFI_LIST_MAX_APS) {
        shown = WIFI_LIST_MAX_APS;
    }

    for (size_t i = 0; i < shown; ++i) {
        const char *src = (const char *)records[i].ssid;
        size_t len = strnlen(src, 32);
        memcpy(ssid_storage[i], src, len);
        ssid_storage[i][len] = '\0';

        if (ssid_storage[i][0] == '\0') {
            continue;
        }

        lv_obj_t *btn = lv_list_add_btn(wifi_list, NULL, ssid_storage[i]);
        lv_obj_add_event_cb(btn, on_ssid_clicked, LV_EVENT_CLICKED, ssid_storage[i]);
    }

    if (shown == 0) {
        lv_list_add_text(wifi_list, "No networks found");
    }
}

void wifi_list_screen_set_status_text(const char *text)
{
    if (!wifi_list_status || !text) {
        return;
    }

    lv_label_set_text(wifi_list_status, text);
}

void wifi_list_screen_set_on_ssid_selected(void (*cb)(const char *ssid))
{
    ssid_selected_cb = cb;
}

void wifi_list_screen_set_on_back(void (*cb)(void))
{
    back_cb = cb;
}

lv_obj_t *wifi_list_screen_get(void)
{
    return wifi_list_screen;
}
