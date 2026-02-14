#include "help_screen.h"

#include "lvgl.h"
#include "extra/libs/qrcode/lv_qrcode.h"

static lv_obj_t *help_screen = NULL;
static void (*back_cb)(void) = NULL;

static const char *HELP_EMAIL = "sanchit.jakhetia@gmail.com";
static const char *HELP_PHONE = "+91 7455888225";
static const char *HELP_HOURS = "Mon-Fri  9:00 AM - 6:00 PM";
static const char *WHATSAPP_LINK = "https://wa.me/917455888225?text=Hello%20DoseRight%20Support";

static void on_help_back_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        if (back_cb) {
            back_cb();
        }
    }
}

void help_screen_init(void)
{
    if (help_screen) {
        return;
    }

    help_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(help_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(help_screen);
    lv_label_set_text(title, "Help & Support");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *back_btn = lv_btn_create(help_screen);
    lv_obj_set_size(back_btn, 34, 28);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 6, 6);
    lv_obj_add_event_cb(back_btn, on_help_back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
    lv_obj_center(back_lbl);

    lv_obj_t *email = lv_label_create(help_screen);
    lv_label_set_text_fmt(email, "Email: %s", HELP_EMAIL);
    lv_obj_align(email, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_text_font(email, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *phone = lv_label_create(help_screen);
    lv_label_set_text_fmt(phone, "Phone: %s", HELP_PHONE);
    lv_obj_align(phone, LV_ALIGN_TOP_MID, 0, 62);
    lv_obj_set_style_text_font(phone, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *hours = lv_label_create(help_screen);
    lv_label_set_text_fmt(hours, "Hours: %s", HELP_HOURS);
    lv_obj_align(hours, LV_ALIGN_TOP_MID, 0, 84);
    lv_obj_set_style_text_font(hours, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_color_t fg = lv_color_hex(0x000000);
    lv_color_t bg = lv_color_hex(0xFFFFFF);
    lv_obj_t *qr = lv_qrcode_create(help_screen, 120, fg, bg);
    lv_obj_align(qr, LV_ALIGN_CENTER, 0, 40);
    lv_qrcode_update(qr, WHATSAPP_LINK, strlen(WHATSAPP_LINK));
}

void help_screen_show(void)
{
    if (!help_screen) {
        help_screen_init();
    }
    lv_scr_load_anim(help_screen, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
}

void help_screen_set_on_back(void (*cb)(void))
{
    back_cb = cb;
}
