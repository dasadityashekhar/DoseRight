#include "alert_screen.h"

#include <stdio.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "alert_audio.h"

static lv_obj_t *alert_screen = NULL;
static lv_obj_t *alert_title_label = NULL;
static lv_obj_t *alert_name_label = NULL;
static lv_obj_t *alert_time_label = NULL;
static lv_obj_t *alert_dose_label = NULL;
static void (*on_pick_cb)(void) = NULL;
static void (*on_skip_cb)(void) = NULL;
static esp_codec_dev_handle_t spk_codec_dev = NULL;
static TaskHandle_t alert_sound_task = NULL;
static volatile bool alert_sound_running = false;

typedef struct {
    const uint8_t *data;
    size_t data_len;
    int sample_rate;
    int channels;
    int bits_per_sample;
} wav_info_t;

static void alert_audio_init(void)
{
    if (spk_codec_dev) {
        return;
    }
    spk_codec_dev = bsp_audio_codec_speaker_init();
    if (spk_codec_dev) {
        esp_codec_dev_set_out_vol(spk_codec_dev, 80);
    }
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static bool parse_wav(const uint8_t *buf, size_t len, wav_info_t *out)
{
    if (!buf || !out || len < 44) {
        return false;
    }
    if (memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) {
        return false;
    }

    bool fmt_found = false;
    bool data_found = false;
    uint32_t offset = 12;

    while (offset + 8 <= len) {
        const uint8_t *chunk_id = buf + offset;
        uint32_t chunk_size = read_le32(buf + offset + 4);
        offset += 8;
        if (offset + chunk_size > len) {
            return false;
        }

        if (memcmp(chunk_id, "fmt ", 4) == 0) {
            if (chunk_size < 16) {
                return false;
            }
            uint16_t audio_format = read_le16(buf + offset);
            out->channels = read_le16(buf + offset + 2);
            out->sample_rate = (int)read_le32(buf + offset + 4);
            out->bits_per_sample = read_le16(buf + offset + 14);
            if (audio_format != 1) {
                return false;
            }
            fmt_found = true;
        } else if (memcmp(chunk_id, "data", 4) == 0) {
            out->data = buf + offset;
            out->data_len = chunk_size;
            data_found = true;
        }

        offset += chunk_size;
        if (chunk_size % 2 != 0) {
            offset += 1;
        }
    }

    return fmt_found && data_found;
}

static void alert_sound_task_fn(void *arg)
{
    (void)arg;
    wav_info_t info = {0};
    if (!parse_wav(alert_audio_wav, alert_audio_wav_len, &info)) {
        alert_sound_running = false;
        vTaskDelete(NULL);
        return;
    }

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = info.sample_rate,
        .channel = info.channels,
        .bits_per_sample = info.bits_per_sample,
    };

    if (spk_codec_dev) {
        esp_codec_dev_open(spk_codec_dev, &fs);
    }

    size_t offset = 0;
    const size_t chunk = 1024;
    while (alert_sound_running) {
        if (spk_codec_dev && info.data && info.data_len > 0) {
            if (offset >= info.data_len) {
                offset = 0;
            }
            size_t remain = info.data_len - offset;
            size_t send = remain < chunk ? remain : chunk;
            esp_codec_dev_write(spk_codec_dev, (void *)(info.data + offset), send);
            offset += send;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (spk_codec_dev) {
        esp_codec_dev_close(spk_codec_dev);
    }
    vTaskDelete(NULL);
}

static void alert_sound_start(void)
{
    if (alert_sound_running) {
        return;
    }
    alert_audio_init();
    if (!spk_codec_dev) {
        return;
    }
    alert_sound_running = true;
    xTaskCreate(alert_sound_task_fn, "alert_sound", 4096, NULL, 5, &alert_sound_task);
}

static void alert_sound_stop(void)
{
    alert_sound_running = false;
    alert_sound_task = NULL;
}

static void on_pick_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    alert_sound_stop();
    if (on_pick_cb) {
        on_pick_cb();
    }
}

static void on_skip_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    alert_sound_stop();
    if (on_skip_cb) {
        on_skip_cb();
    }
}

void alert_screen_set_on_pick(void (*cb)(void))
{
    on_pick_cb = cb;
}

void alert_screen_set_on_skip(void (*cb)(void))
{
    on_skip_cb = cb;
}

void alert_screen_init(void)
{
    if (alert_screen) {
        return;
    }

    alert_audio_init();

    alert_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(alert_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(alert_screen, lv_color_hex(0xCC0000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(alert_screen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    alert_title_label = lv_label_create(alert_screen);
    lv_label_set_text(alert_title_label, "MEDICINE ALERT");
    lv_obj_align(alert_title_label, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_set_style_text_color(alert_title_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(alert_title_label, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    alert_name_label = lv_label_create(alert_screen);
    lv_obj_align(alert_name_label, LV_ALIGN_CENTER, 0, -24);
    lv_obj_set_style_text_color(alert_name_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(alert_name_label, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(alert_name_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_width(alert_name_label, 280);
    lv_label_set_long_mode(alert_name_label, LV_LABEL_LONG_WRAP);

    alert_time_label = lv_label_create(alert_screen);
    lv_obj_align(alert_time_label, LV_ALIGN_CENTER, 0, 6);
    lv_obj_set_style_text_color(alert_time_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(alert_time_label, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);

    alert_dose_label = lv_label_create(alert_screen);
    lv_obj_align(alert_dose_label, LV_ALIGN_CENTER, 0, 40);
    lv_obj_set_style_text_color(alert_dose_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(alert_dose_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *btn_row = lv_obj_create(alert_screen);
    lv_obj_set_size(btn_row, 280, 52);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn_row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *pick_btn = lv_btn_create(btn_row);
    lv_obj_set_size(pick_btn, 120, 44);
    lv_obj_align(pick_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(pick_btn, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(pick_btn, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(pick_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(pick_btn, on_pick_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *pick_lbl = lv_label_create(pick_btn);
    lv_label_set_text(pick_lbl, "Pick");
    lv_obj_set_style_text_color(pick_lbl, lv_color_hex(0xB00000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(pick_lbl);

    lv_obj_t *skip_btn = lv_btn_create(btn_row);
    lv_obj_set_size(skip_btn, 120, 44);
    lv_obj_align(skip_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(skip_btn, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(skip_btn, lv_color_hex(0x8A0000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(skip_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(skip_btn, on_skip_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *skip_lbl = lv_label_create(skip_btn);
    lv_label_set_text(skip_lbl, "Skip");
    lv_obj_set_style_text_color(skip_lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(skip_lbl);
}

void alert_screen_show(const char *name, const char *time_str, const char *dose)
{
    if (!alert_screen) {
        alert_screen_init();
    }

    lv_label_set_text(alert_name_label, (name && name[0]) ? name : "--");
    lv_label_set_text(alert_time_label, (time_str && time_str[0]) ? time_str : "--:--");

    if (dose && dose[0]) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Dose: %s", dose);
        lv_label_set_text(alert_dose_label, buf);
    } else {
        lv_label_set_text(alert_dose_label, "Dose: --");
    }

    alert_sound_start();

    lv_scr_load_anim(alert_screen, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
}
