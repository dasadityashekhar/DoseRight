#include "settings_screen.h"

#include <stdio.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "esp_system.h"

static lv_obj_t *settings_screen = NULL;
static lv_obj_t *buzzer_slider = NULL;
static lv_obj_t *buzzer_value = NULL;
static lv_obj_t *brightness_slider = NULL;
static lv_obj_t *brightness_value = NULL;
static lv_obj_t *confirm_box = NULL;
static void (*back_cb)(void) = NULL;

static const int BRIGHTNESS_MIN = 10;

static void on_back_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (back_cb) {
        back_cb();
    }
}

static void update_buzzer_label(int value)
{
    if (!buzzer_value) {
        return;
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", value);
    lv_label_set_text(buzzer_value, buf);
}

static void update_brightness_label(int value)
{
    if (!brightness_value) {
        return;
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", value);
    lv_label_set_text(brightness_value, buf);
}

static void on_buzzer_slider(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    int value = lv_slider_get_value(buzzer_slider);
    update_buzzer_label(value);
}

static void on_brightness_slider(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    int value = lv_slider_get_value(brightness_slider);
    if (value < BRIGHTNESS_MIN) {
        value = BRIGHTNESS_MIN;
        lv_slider_set_value(brightness_slider, value, LV_ANIM_OFF);
    }
    update_brightness_label(value);
    bsp_display_brightness_set(value);
}

static void on_restart_confirm(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        const char *txt = lv_msgbox_get_active_btn_text(confirm_box);
        if (txt && strcmp(txt, "Restart") == 0) {
            esp_restart();
        }
        lv_obj_del(confirm_box);
        confirm_box = NULL;
    }
}

static void on_restart_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    if (confirm_box) {
        return;
    }

    static const char *btns[] = {"Cancel", "Restart", ""};
    confirm_box = lv_msgbox_create(settings_screen, "Restart Device", "Are you sure?", btns, false);
    lv_obj_center(confirm_box);
    lv_obj_add_event_cb(confirm_box, on_restart_confirm, LV_EVENT_VALUE_CHANGED, NULL);
}

void settings_screen_set_on_back(void (*cb)(void))
{
    back_cb = cb;
}

void settings_screen_init(void)
{
    if (settings_screen) {
        return;
    }

    settings_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(settings_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(settings_screen);
    lv_label_set_text(title, "Settings");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *back_btn = lv_btn_create(settings_screen);
    lv_obj_set_size(back_btn, 34, 28);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 6, 6);
    lv_obj_add_event_cb(back_btn, on_back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
    lv_obj_center(back_lbl);

    lv_obj_t *buzzer_label = lv_label_create(settings_screen);
    lv_label_set_text(buzzer_label, "Buzzer Volume");
    lv_obj_align(buzzer_label, LV_ALIGN_TOP_LEFT, 16, 50);

    buzzer_slider = lv_slider_create(settings_screen);
    lv_obj_set_width(buzzer_slider, 220);
    lv_obj_align(buzzer_slider, LV_ALIGN_TOP_LEFT, 16, 70);
    lv_slider_set_range(buzzer_slider, 0, 100);
    lv_slider_set_value(buzzer_slider, 100, LV_ANIM_OFF);
    lv_obj_add_event_cb(buzzer_slider, on_buzzer_slider, LV_EVENT_VALUE_CHANGED, NULL);

    buzzer_value = lv_label_create(settings_screen);
    lv_obj_align(buzzer_value, LV_ALIGN_TOP_RIGHT, -16, 70);
    update_buzzer_label(100);

    lv_obj_t *bright_label = lv_label_create(settings_screen);
    lv_label_set_text(bright_label, "Screen Brightness");
    lv_obj_align(bright_label, LV_ALIGN_TOP_LEFT, 16, 110);

    brightness_slider = lv_slider_create(settings_screen);
    lv_obj_set_width(brightness_slider, 220);
    lv_obj_align(brightness_slider, LV_ALIGN_TOP_LEFT, 16, 130);
    lv_slider_set_range(brightness_slider, BRIGHTNESS_MIN, 100);
    lv_slider_set_value(brightness_slider, 100, LV_ANIM_OFF);
    lv_obj_add_event_cb(brightness_slider, on_brightness_slider, LV_EVENT_VALUE_CHANGED, NULL);

    brightness_value = lv_label_create(settings_screen);
    lv_obj_align(brightness_value, LV_ALIGN_TOP_RIGHT, -16, 130);
    update_brightness_label(100);
    bsp_display_brightness_set(100);

    lv_obj_t *restart_btn = lv_btn_create(settings_screen);
    lv_obj_set_size(restart_btn, 200, 36);
    lv_obj_align(restart_btn, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_add_event_cb(restart_btn, on_restart_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *restart_lbl = lv_label_create(restart_btn);
    lv_label_set_text(restart_lbl, "Restart Device");
    lv_obj_center(restart_lbl);
}

void settings_screen_show(void)
{
    if (!settings_screen) {
        settings_screen_init();
    }
    lv_scr_load_anim(settings_screen, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
}
