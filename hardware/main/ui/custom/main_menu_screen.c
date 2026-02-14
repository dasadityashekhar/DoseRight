#include "main_menu_screen.h"

#include <string.h>

#include "ui/ui.h"

static lv_obj_t *menu_parent = NULL;
static lv_obj_t *menu_heading_label = NULL;
static lv_obj_t *menu_list = NULL;
static lv_obj_t *menu_buttons[MAIN_MENU_COUNT] = {0};
static lv_obj_t *menu_back_btn = NULL;
static void (*menu_select_cb)(main_menu_item_t item) = NULL;
static void (*menu_back_cb)(void) = NULL;

static void on_menu_item_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    uintptr_t idx = (uintptr_t)lv_event_get_user_data(e);
    if (menu_select_cb) {
        menu_select_cb((main_menu_item_t)idx);
    }
}

static void on_back_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    if (menu_back_cb) {
        menu_back_cb();
    }
}

void main_menu_screen_set_on_select(void (*cb)(main_menu_item_t item))
{
    menu_select_cb = cb;
}

void main_menu_screen_set_on_back(void (*cb)(void))
{
    menu_back_cb = cb;
}

void main_menu_screen_init(lv_obj_t *parent)
{
    if (!parent) {
        return;
    }

    if (menu_parent != parent) {
        menu_parent = parent;
        menu_heading_label = NULL;
        menu_list = NULL;
        menu_back_btn = NULL;
        memset(menu_buttons, 0, sizeof(menu_buttons));
    }

    if (!menu_heading_label) {
        menu_heading_label = lv_label_create(parent);
        lv_label_set_text(menu_heading_label, "MAIN MENU");
        lv_obj_align(menu_heading_label, LV_ALIGN_TOP_MID, 0, 10);
        lv_obj_set_style_text_font(menu_heading_label, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    if (!menu_back_btn) {
        menu_back_btn = lv_btn_create(parent);
        lv_obj_set_size(menu_back_btn, 34, 28);
        lv_obj_align(menu_back_btn, LV_ALIGN_TOP_LEFT, 6, 6);
        lv_obj_add_event_cb(menu_back_btn, on_back_clicked, LV_EVENT_CLICKED, NULL);

        lv_obj_t *back_lbl = lv_label_create(menu_back_btn);
        lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
        lv_obj_center(back_lbl);
    }

    if (!menu_list) {
        menu_list = lv_list_create(parent);
        lv_obj_set_size(menu_list, 280, 180);
        lv_obj_align(menu_list, LV_ALIGN_BOTTOM_MID, 0, -8);
        lv_obj_set_scrollbar_mode(menu_list, LV_SCROLLBAR_MODE_AUTO);
    }

    static const char *labels[MAIN_MENU_COUNT] = {
        "1. TAKEN",
        "2. UPCOMING",
        "3. MISSED",
        "4. REFILL",
        "5. CALIBRATE",
        "6. SETTINGS",
        "7. HELP"
    };

    for (size_t i = 0; i < MAIN_MENU_COUNT; ++i) {
        if (!menu_buttons[i]) {
            menu_buttons[i] = lv_list_add_btn(menu_list, NULL, labels[i]);
            lv_obj_add_event_cb(menu_buttons[i], on_menu_item_clicked, LV_EVENT_CLICKED, (void *)i);
        }
    }

    if (ui_Label9) {
        lv_obj_add_flag(ui_Label9, LV_OBJ_FLAG_HIDDEN);
    }
    if (ui_Label6) {
        lv_obj_add_flag(ui_Label6, LV_OBJ_FLAG_HIDDEN);
    }
    if (ui_Label5) {
        lv_obj_add_flag(ui_Label5, LV_OBJ_FLAG_HIDDEN);
    }
    if (ui_Button2) {
        lv_obj_add_flag(ui_Button2, LV_OBJ_FLAG_HIDDEN);
    }
}
