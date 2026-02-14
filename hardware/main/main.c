#include "bsp/esp-bsp.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_rom_sys.h"
#include "iot_button.h"

#include "lvgl.h"
#include "esp_lvgl_port.h"

#include "ui/ui.h"
#include "ui/custom/wifi_list_screen.h"
#include "ui/custom/main_menu_screen.h"
#include "ui/custom/help_screen.h"
#include "ui/custom/settings_screen.h"
#include "ui/custom/profile_screen.h"
#include "ui/custom/alert_screen.h"

static const char *TAG = "DoseRight";

// ---------- BACKEND CONFIG ----------
// NOTE: Fill these locally before building; do not commit real values.
static const char *BACKEND_BASE_URL = "";
static const char *DEVICE_ID = "";
static const char *DEVICE_SECRET = "";
static const char *FIRMWARE_VERSION = "1.2.3";
static const char *TIME_API_PATH = "/api/hardware/time";

#define STEPPER_TOTAL_SLOTS 5
#define STEPPER_STEPS_PER_REV 2048
#define STEPPER_STEPS_PER_SLOT (STEPPER_STEPS_PER_REV / STEPPER_TOTAL_SLOTS)

static const int DEMO_BATTERY_LEVEL = 87;
static const int DEMO_STORAGE_FREE_KB = 812;
static const float DEMO_TEMPERATURE_C = 36.8f;

static const int64_t BACKEND_FETCH_INTERVAL_MS = 60000;
static const int64_t HEARTBEAT_INTERVAL_MS = 60000;

static lv_obj_t *clock_label = NULL;
static lv_obj_t *wifi_status_label = NULL;
static lv_obj_t *profile_btn = NULL;
static lv_obj_t *menu_screen = NULL;
static lv_obj_t *refill_screen = NULL;
static lv_obj_t *refill_title_label = NULL;
static lv_obj_t *refill_back_btn = NULL;
static lv_obj_t *refill_list = NULL;
static lv_obj_t *refill_buttons[STEPPER_TOTAL_SLOTS] = {0};
static lv_obj_t *refill_status_label = NULL;
static lv_obj_t *calibrate_menu_screen = NULL;
static lv_obj_t *calibrate_screen = NULL;
static lv_obj_t *calibrate_status_label = NULL;
static lv_timer_t *calibrate_move_timer = NULL;
static lv_obj_t *servo_screen = NULL;
static lv_obj_t *servo_slider = NULL;
static lv_obj_t *servo_value_label = NULL;
static lv_obj_t *servo_status_label = NULL;
static lv_timer_t *servo_move_timer = NULL;
static int servo_target_deg = 90;
static int servo_current_deg = 90;
static int calibrate_move_dir = 0;
static lv_timer_t *boot_progress_timer = NULL;
static int boot_progress_value = 0;
static lv_obj_t *main_med_name_label = NULL;
static lv_obj_t *main_med_time_label = NULL;
static lv_obj_t *main_med_dose_label = NULL;
static lv_obj_t *main_med_heading_label = NULL;
static lv_obj_t *main_med_time_heading_label = NULL;
static lv_obj_t *swipe_hint_label = NULL;
static lv_obj_t *info_screen = NULL;
static lv_obj_t *info_label = NULL;
static lv_obj_t *info_back_btn = NULL;
static lv_obj_t *info_list = NULL;
static lv_obj_t *qr_screen = NULL;
static lv_obj_t *qr_code = NULL;
static lv_obj_t *qr_device_label = NULL;
static lv_obj_t *qr_next_btn = NULL;
static char current_info_path[32] = {0};
static char current_info_title[16] = {0};
static char pending_info_path[32] = {0};
static char pending_info_title[16] = {0};
static volatile bool pending_info_fetch = false;
static char selected_ssid[33] = {0};
static char selected_password[65] = {0};
static bool wifi_ready = false;
static bool wifi_auto_connecting = false;
static int wifi_auto_index = -1;
static button_handle_t main_button = NULL;
static volatile bool backend_fetch_requested = false;
static char backend_last_error[64] = "Fetch failed";
static volatile bool time_sync_requested = false;
static bool time_synced = false;
static int64_t time_base_epoch_seconds = 0;
static int32_t time_base_offset_min = 0;
static int64_t time_base_ms = 0;
static char time_display[16] = "--:--";
static bool time_display_valid = false;
static int64_t last_time_sync_ms = 0;
static int time_base_hour24 = -1;
static int time_base_minute = -1;

static const int64_t TIME_RESYNC_INTERVAL_MS = 10 * 60 * 1000;
static const uint32_t BOOT_PROGRESS_INTERVAL_MS = 50;

static void log_http_response(const char *context, const char *url, int status, const char *body, int body_len)
{
    const int max_preview = 1024;
    int log_len = body_len;
    if (!body) {
        ESP_LOGI(TAG, "%s response: url=%s status=%d (no body)", context, url ? url : "(null)", status);
        return;
    }
    if (log_len < 0) {
        log_len = (int)strlen(body);
    }
    ESP_LOGI(TAG, "%s response: url=%s status=%d body_len=%d", context, url ? url : "(null)", status, log_len);
    if (log_len <= 0) {
        return;
    }
    if (log_len > max_preview) {
        log_len = max_preview;
    }
    char preview[max_preview + 1];
    memcpy(preview, body, (size_t)log_len);
    preview[log_len] = '\0';
    ESP_LOGI(TAG, "%s body: %s", context, preview);
    if (body_len > max_preview) {
        ESP_LOGI(TAG, "%s body truncated", context);
    }
}

#define MOTOR_IN1_GPIO 14
#define MOTOR_IN2_GPIO 10
#define MOTOR_IN3_GPIO 42
#define MOTOR_IN4_GPIO 13

#define SERVO_GPIO 21
#define SERVO_MIN_US 1000
#define SERVO_MAX_US 2000
#define SERVO_PERIOD_US 20000
#define SERVO_LEDC_MODE LEDC_LOW_SPEED_MODE
#define SERVO_LEDC_TIMER LEDC_TIMER_0
#define SERVO_LEDC_CHANNEL LEDC_CHANNEL_0
#define SERVO_LEDC_RESOLUTION LEDC_TIMER_14_BIT
#define SERVO_DEG_MAX 180
#define SERVO_SWEEP_MAX_DEG 90
#define SERVO_STEP_DEG 1
#define SERVO_STEP_INTERVAL_US 20000

#define IR_SENSOR_GPIO 12
#define IR_POLL_INTERVAL_US 500000

#define CALIBRATE_STEP_COUNT 20
#define CALIBRATE_STEP_DELAY_US 2000
#define CALIBRATE_CONT_STEP 1
#define CALIBRATE_CONT_INTERVAL_MS 50

#define MED_CACHE_MAX 10
#define WIFI_CRED_MAX 3

typedef struct {
    char ssid[33];
    char password[65];
} wifi_cred_t;

typedef struct {
    uint8_t count;
    wifi_cred_t creds[WIFI_CRED_MAX];
} wifi_cred_store_t;

typedef struct {
    char name[48];
    char dose[32];
    char time_str[8];
    char status[16];
    char dose_id[40];
    int slot;
} med_cache_item_t;

typedef struct {
    med_cache_item_t items[MED_CACHE_MAX];
    size_t count;
    char updated[16];
    bool valid;
} med_cache_t;

static med_cache_t cache_taken = {0};
static med_cache_t cache_upcoming = {0};
static med_cache_t cache_missed = {0};
static wifi_cred_t wifi_creds[WIFI_CRED_MAX] = {0};
static size_t wifi_creds_count = 0;

static int motor_step_index = 0;
static int stepper_current_steps = 0;
static int stepper_current_slot = 1;
static esp_timer_handle_t servo_timer = NULL;
static bool servo_left = false;
static int servo_deg = 0;
static esp_timer_handle_t ir_timer = NULL;
static esp_timer_handle_t ir_close_timer = NULL;
static int last_ir_state = -1;
static bool ir_enabled = false;
static bool servo_enable_ir_on_complete = false;
static bool ir_close_pending = false;
static bool ir_detected_once = false;
static bool pending_mark_taken = false;
static char current_alert_dose_id[40] = {0};

static void med_cache_save_nvs(const char *key, const med_cache_t *cache)
{
    if (!key || !cache) {
        return;
    }
    nvs_handle_t handle;
    if (nvs_open("doseright", NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    nvs_set_blob(handle, key, cache, sizeof(*cache));
    nvs_commit(handle);
    nvs_close(handle);
}

static void med_cache_load_nvs(const char *key, med_cache_t *cache)
{
    if (!key || !cache) {
        return;
    }
    nvs_handle_t handle;
    if (nvs_open("doseright", NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    size_t size = sizeof(*cache);
    if (nvs_get_blob(handle, key, cache, &size) == ESP_OK) {
        cache->valid = true;
    }
    nvs_close(handle);
}

static void med_cache_load_all(void)
{
    med_cache_load_nvs("med_taken", &cache_taken);
    med_cache_load_nvs("med_upcoming", &cache_upcoming);
    med_cache_load_nvs("med_missed", &cache_missed);
}

static void time_cache_save_nvs(const char *time_str)
{
    if (!time_str || time_str[0] == '\0') {
        return;
    }
    nvs_handle_t handle;
    if (nvs_open("doseright", NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    nvs_set_str(handle, "time_display", time_str);
    nvs_commit(handle);
    nvs_close(handle);
}

static void time_cache_load_nvs(void)
{
    nvs_handle_t handle;
    if (nvs_open("doseright", NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    char buf[16] = {0};
    size_t len = sizeof(buf);
    if (nvs_get_str(handle, "time_display", buf, &len) == ESP_OK && len > 1) {
        snprintf(time_display, sizeof(time_display), "%s", buf);
        time_display_valid = true;
    }
    nvs_close(handle);
}

static void wifi_creds_load(void)
{
    wifi_creds_count = 0;
    nvs_handle_t handle;
    if (nvs_open("doseright", NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    wifi_cred_store_t store = {0};
    size_t size = sizeof(store);
    if (nvs_get_blob(handle, "wifi_creds", &store, &size) == ESP_OK) {
        if (store.count > WIFI_CRED_MAX) {
            store.count = WIFI_CRED_MAX;
        }
        wifi_creds_count = store.count;
        for (size_t i = 0; i < wifi_creds_count; ++i) {
            wifi_creds[i] = store.creds[i];
        }
    }
    nvs_close(handle);
}

static void wifi_creds_save(void)
{
    nvs_handle_t handle;
    if (nvs_open("doseright", NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    wifi_cred_store_t store = {0};
    store.count = (uint8_t)wifi_creds_count;
    for (size_t i = 0; i < wifi_creds_count; ++i) {
        store.creds[i] = wifi_creds[i];
    }
    nvs_set_blob(handle, "wifi_creds", &store, sizeof(store));
    nvs_commit(handle);
    nvs_close(handle);
}

static void wifi_creds_add_or_update(const char *ssid, const char *password)
{
    if (!ssid || ssid[0] == '\0') {
        return;
    }

    size_t idx = wifi_creds_count;
    for (size_t i = 0; i < wifi_creds_count; ++i) {
        if (strncmp(wifi_creds[i].ssid, ssid, sizeof(wifi_creds[i].ssid)) == 0) {
            idx = i;
            break;
        }
    }

    if (idx >= wifi_creds_count) {
        if (wifi_creds_count < WIFI_CRED_MAX) {
            wifi_creds_count++;
        }
        idx = wifi_creds_count - 1;
    }

    for (size_t i = idx; i > 0; --i) {
        wifi_creds[i] = wifi_creds[i - 1];
    }

    snprintf(wifi_creds[0].ssid, sizeof(wifi_creds[0].ssid), "%s", ssid);
    snprintf(wifi_creds[0].password, sizeof(wifi_creds[0].password), "%s", password ? password : "");
    wifi_creds_save();
}

static void wifi_connect_start(const char *ssid, const char *password, bool auto_attempt)
{
    if (!wifi_ready || !ssid || ssid[0] == '\0') {
        return;
    }

    snprintf(selected_ssid, sizeof(selected_ssid), "%s", ssid);
    snprintf(selected_password, sizeof(selected_password), "%s", password ? password : "");

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, selected_ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, selected_password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;

    if (!auto_attempt) {
        wifi_auto_connecting = false;
        wifi_auto_index = -1;
    }

    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_connect();
}

static bool wifi_auto_connect_start(void)
{
    if (wifi_creds_count == 0) {
        return false;
    }
    wifi_auto_connecting = true;
    wifi_auto_index = 0;
    wifi_connect_start(wifi_creds[0].ssid, wifi_creds[0].password, true);
    return true;
}

static bool wifi_auto_connect_next(void)
{
    if (!wifi_auto_connecting) {
        return false;
    }
    wifi_auto_index++;
    if (wifi_auto_index < 0 || wifi_auto_index >= (int)wifi_creds_count) {
        wifi_auto_connecting = false;
        wifi_auto_index = -1;
        return false;
    }
    wifi_connect_start(wifi_creds[wifi_auto_index].ssid, wifi_creds[wifi_auto_index].password, true);
    return true;
}

static const char *get_cache_key_for_path(const char *path)
{
    if (!path) {
        return NULL;
    }
    if (strcmp(path, "/api/hardware/taken") == 0) {
        return "med_taken";
    }
    if (strcmp(path, "/api/hardware/upcoming") == 0) {
        return "med_upcoming";
    }
    if (strcmp(path, "/api/hardware/missed") == 0) {
        return "med_missed";
    }
    return NULL;
}

static void ensure_med_cache_loaded(const char *path, med_cache_t *cache)
{
    if (!cache || cache->valid) {
        return;
    }
    const char *key = get_cache_key_for_path(path);
    if (key) {
        med_cache_load_nvs(key, cache);
    }
}

static void wifi_start_scan(void);
static void backend_fetch_task(void *arg);
static void time_sync_task(void *arg);
static void on_wifi_logo_clicked(lv_event_t *e);
static void on_main_menu_selected(main_menu_item_t item);
static void on_info_back_clicked(lv_event_t *e);
static void ensure_menu_screen(void);
static void show_menu_screen(void);
static void on_main_menu_back(void);
static void on_submenu_back(void);
static void on_main_screen_gesture(lv_event_t *e);
static void show_info_screen(const char *title);
static void fetch_and_show_meds(const char *path, const char *title);
static bool wifi_is_connected(void);
static void ensure_qr_screen(void);
static void show_qr_screen(void);
static void on_qr_next_clicked(lv_event_t *e);
static void on_profile_clicked(lv_event_t *e);
static void render_med_cache(const char *title, const med_cache_t *cache, bool offline);
static med_cache_t *get_cache_for_path(const char *path);
static bool apply_cached_upcoming_to_main(void);
static void route_to_screen3(void);
static void on_alert_pick_action(void);
static void on_alert_skip_action(void);
static void show_calibrate_screen(void);
static void ensure_calibrate_screen(void);
static void ensure_calibrate_menu_screen(void);
static void show_calibrate_menu_screen(void);
static void on_calibrate_menu_back_clicked(lv_event_t *e);
static void on_stepper_option_clicked(lv_event_t *e);
static void on_servo_option_clicked(lv_event_t *e);
static void ensure_servo_calibrate_screen(void);
static void show_servo_calibrate_screen(void);
static void on_servo_back_clicked(lv_event_t *e);
static void on_servo_slider_changed(lv_event_t *e);
static void on_servo_move_clicked(lv_event_t *e);
static void motor_init(void);
static void motor_move_steps(int steps);
static void stepper_move_to_slot(int slot);
static void stepper_slot_load(void);
static void stepper_slot_save(int slot);
static void on_calibrate_move_event(lv_event_t *e);
static void on_calibrate_move_timer(lv_timer_t *timer);
static void servo_move_timer_cb(lv_timer_t *timer);
static void boot_progress_timer_cb(lv_timer_t *timer);
static void wifi_creds_load(void);
static void wifi_creds_save(void);
static void wifi_creds_add_or_update(const char *ssid, const char *password);
static bool wifi_auto_connect_start(void);
static bool wifi_auto_connect_next(void);
static void wifi_connect_start(const char *ssid, const char *password, bool auto_attempt);
static void init_main_button(void);
static void on_main_button_click(void *btn, void *arg);
static void format_time_12h(const char *src, char *dst, size_t dst_size);
static int time_12h_to_minutes(const char *src);
static int time_any_to_minutes(const char *src);
static int get_current_time_minutes(void);
static void check_medicine_alert(void);
static bool backend_fetch_cache(const char *path, med_cache_t *cache, const char *cache_key);
static void servo_init(void);
static void servo_set_pulse_us(uint32_t pulse_us);
static void servo_set_degree(int degree);
static void servo_test_timer_cb(void *arg);
static void servo_test_start(void);
static void servo_test_stop(void);
static void servo_start_move(int target_deg, bool enable_ir_on_complete);
static void ir_sensor_init(void);
static void ir_sensor_timer_cb(void *arg);
static void ir_sensor_start(void);
static void ir_sensor_set_enabled(bool enabled);
static void ir_close_timer_cb(void *arg);
static void heartbeat_task(void *arg);
static bool send_heartbeat(void);
static int get_wifi_strength(void);
static int get_battery_level(void);
static void send_dose_event_async(bool taken);
static bool send_dose_event(const char *dose_id, bool taken);
static void dose_event_task(void *arg);
static void log_wifi_status(const char *context);

static void update_clock_text(void)
{
    if (!clock_label) {
        return;
    }

    if (!time_synced) {
        lv_label_set_text(clock_label, time_display_valid ? time_display : "--:--");
        return;
    }

    if (time_base_ms == 0 || time_base_hour24 < 0 || time_base_minute < 0) {
        lv_label_set_text(clock_label, time_display);
        return;
    }

    int64_t now_ms = esp_timer_get_time() / 1000;
    int64_t elapsed_minutes = (now_ms - time_base_ms) / 60000;
    int total_minutes = (time_base_hour24 * 60 + time_base_minute + (int)elapsed_minutes) % (24 * 60);
    if (total_minutes < 0) {
        total_minutes += 24 * 60;
    }

    int hour24 = total_minutes / 60;
    int minute = total_minutes % 60;
    const char *ampm = (hour24 >= 12) ? "PM" : "AM";
    int hour12 = hour24 % 12;
    if (hour12 == 0) {
        hour12 = 12;
    }

    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d %s", hour12, minute, ampm);
    lv_label_set_text(clock_label, buf);
    snprintf(time_display, sizeof(time_display), "%s", buf);
    time_display_valid = true;
}

static void format_time_12h(const char *src, char *dst, size_t dst_size)
{
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src || src[0] == '\0') {
        snprintf(dst, dst_size, "--:--");
        return;
    }

    int hour = -1;
    int minute = -1;
    int second = 0;
    if (sscanf(src, "%d:%d:%d", &hour, &minute, &second) == 3) {
        if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
            snprintf(dst, dst_size, "%s", src);
            return;
        }
        const char *ampm = (hour >= 12) ? "PM" : "AM";
        int hour12 = hour % 12;
        if (hour12 == 0) {
            hour12 = 12;
        }
        snprintf(dst, dst_size, "%02d:%02d %s", hour12, minute, ampm);
        return;
    }

    if (sscanf(src, "%d:%d", &hour, &minute) == 2) {
        if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
            snprintf(dst, dst_size, "%s", src);
            return;
        }
        const char *ampm = (hour >= 12) ? "PM" : "AM";
        int hour12 = hour % 12;
        if (hour12 == 0) {
            hour12 = 12;
        }
        snprintf(dst, dst_size, "%02d:%02d %s", hour12, minute, ampm);
        return;
    }

    snprintf(dst, dst_size, "%s", src);
}

static int time_12h_to_minutes(const char *src)
{
    if (!src || src[0] == '\0') {
        return -1;
    }
    int hour = 0;
    int minute = 0;
    int second = 0;
    char ampm[3] = {0};
    if (sscanf(src, "%d:%d:%d %2s", &hour, &minute, &second, ampm) == 4) {
        if (minute < 0 || minute > 59 || hour < 1 || hour > 12) {
            return -1;
        }
        bool is_pm = (ampm[0] == 'P' || ampm[0] == 'p');
        int hour24 = hour % 12;
        if (is_pm) {
            hour24 += 12;
        }
        return hour24 * 60 + minute;
    }

    if (sscanf(src, "%d:%d %2s", &hour, &minute, ampm) != 3) {
        return -1;
    }
    if (minute < 0 || minute > 59 || hour < 1 || hour > 12) {
        return -1;
    }
    bool is_pm = (ampm[0] == 'P' || ampm[0] == 'p');
    int hour24 = hour % 12;
    if (is_pm) {
        hour24 += 12;
    }
    return hour24 * 60 + minute;
}

static int time_any_to_minutes(const char *src)
{
    if (!src || src[0] == '\0') {
        return -1;
    }
    int hour = 0;
    int minute = 0;
    int second = 0;
    char ampm[3] = {0};

    if (sscanf(src, "%d:%d:%d %2s", &hour, &minute, &second, ampm) == 4) {
        if (minute < 0 || minute > 59 || hour < 1 || hour > 12) {
            return -1;
        }
        bool is_pm = (ampm[0] == 'P' || ampm[0] == 'p');
        int hour24 = hour % 12;
        if (is_pm) {
            hour24 += 12;
        }
        return hour24 * 60 + minute;
    }

    if (sscanf(src, "%d:%d %2s", &hour, &minute, ampm) == 3) {
        if (minute < 0 || minute > 59 || hour < 1 || hour > 12) {
            return -1;
        }
        bool is_pm = (ampm[0] == 'P' || ampm[0] == 'p');
        int hour24 = hour % 12;
        if (is_pm) {
            hour24 += 12;
        }
        return hour24 * 60 + minute;
    }

    if (sscanf(src, "%d:%d:%d", &hour, &minute, &second) == 3) {
        if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
            return -1;
        }
        return hour * 60 + minute;
    }

    if (sscanf(src, "%d:%d", &hour, &minute) == 2) {
        if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
            return -1;
        }
        return hour * 60 + minute;
    }

    return -1;
}

static int get_current_time_minutes(void)
{
    if (time_synced && time_base_ms != 0 && time_base_hour24 >= 0 && time_base_minute >= 0) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        int64_t elapsed_minutes = (now_ms - time_base_ms) / 60000;
        int total_minutes = (time_base_hour24 * 60 + time_base_minute + (int)elapsed_minutes) % (24 * 60);
        if (total_minutes < 0) {
            total_minutes += 24 * 60;
        }
        return total_minutes;
    }
    if (time_display_valid) {
        return time_12h_to_minutes(time_display);
    }
    return -1;
}

static void check_medicine_alert(void)
{
    static int last_alert_minute = -1;
    static char last_alert_name[48] = {0};

    if (!time_display_valid || !cache_upcoming.valid || cache_upcoming.count == 0) {
        return;
    }
    const med_cache_item_t *item = &cache_upcoming.items[0];
    if (!item->time_str[0]) {
        return;
    }

    int now_minutes = get_current_time_minutes();
    int med_minutes = time_any_to_minutes(item->time_str);
    if (now_minutes < 0 || med_minutes < 0) {
        return;
    }

    if (now_minutes == med_minutes) {
        if (last_alert_minute != now_minutes || strcmp(last_alert_name, item->name) != 0) {
            snprintf(last_alert_name, sizeof(last_alert_name), "%s", item->name);
            last_alert_minute = now_minutes;
            snprintf(current_alert_dose_id, sizeof(current_alert_dose_id), "%s", item->dose_id);
            if (current_alert_dose_id[0] == '\0') {
                ESP_LOGW(TAG, "Upcoming dose missing doseId; cannot mark taken/skip (slot=%d)", item->slot);
            }
            stepper_move_to_slot(item->slot);
            alert_screen_show(item->name, item->time_str, item->dose);
        }
    }
}

static void clock_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    update_clock_text();
    check_medicine_alert();
}

static void ensure_clock_label(void)
{
    if (!ui_Screen3) {
        return;
    }

    if (!clock_label) {
        clock_label = lv_label_create(ui_Screen3);
        lv_obj_align(clock_label, LV_ALIGN_TOP_MID, 0, 6);
        lv_obj_set_style_text_font(clock_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    update_clock_text();
}

static void ensure_profile_button(void)
{
    if (!ui_Screen3) {
        return;
    }

    if (!profile_btn) {
        profile_btn = lv_btn_create(ui_Screen3);
        lv_obj_set_size(profile_btn, 26, 26);
        lv_obj_align(profile_btn, LV_ALIGN_TOP_LEFT, 6, 6);
        lv_obj_add_event_cb(profile_btn, on_profile_clicked, LV_EVENT_CLICKED, NULL);

        lv_obj_t *label = lv_label_create(profile_btn);
        lv_label_set_text(label, LV_SYMBOL_DIRECTORY);
        lv_obj_center(label);
    }
}

static void on_profile_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        profile_screen_show();
    }
}

static void set_wifi_status_state(bool connected, const char *text)
{
    (void)text;
    if (!wifi_status_label) {
        return;
    }

    lv_label_set_text(wifi_status_label, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(
        wifi_status_label,
        connected ? lv_color_hex(0x00AA00) : lv_color_hex(0xCC0000),
        LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void ensure_wifi_status_label(void)
{
    if (!ui_Screen3) {
        return;
    }

    if (wifi_status_label && lv_obj_get_parent(wifi_status_label) != ui_Screen3) {
        wifi_status_label = NULL;
    }

    if (!wifi_status_label) {
        wifi_status_label = lv_label_create(ui_Screen3);
        lv_obj_align(wifi_status_label, LV_ALIGN_TOP_RIGHT, -10, 6);
        lv_obj_set_style_text_font(wifi_status_label, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_flag(wifi_status_label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(wifi_status_label, on_wifi_logo_clicked, LV_EVENT_CLICKED, NULL);
    }

    set_wifi_status_state(false, NULL);
}

static void ensure_main_data_labels(void)
{
    if (!ui_Screen3) {
        return;
    }

    if (!main_med_heading_label) {
        main_med_heading_label = lv_label_create(ui_Screen3);
        lv_obj_align(main_med_heading_label, LV_ALIGN_CENTER, 0, -60);
        lv_obj_set_style_text_font(main_med_heading_label, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(main_med_heading_label, "NEXT MEDICINE");
    }

    if (!main_med_name_label) {
        main_med_name_label = lv_label_create(ui_Screen3);
        lv_obj_set_width(main_med_name_label, 300);
        lv_label_set_long_mode(main_med_name_label, LV_LABEL_LONG_WRAP);
        lv_obj_align(main_med_name_label, LV_ALIGN_CENTER, 0, -35);
        lv_obj_set_style_text_font(main_med_name_label, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(main_med_name_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    if (!main_med_time_heading_label) {
        main_med_time_heading_label = lv_label_create(ui_Screen3);
        lv_obj_align(main_med_time_heading_label, LV_ALIGN_CENTER, 0, -5);
        lv_obj_set_style_text_font(main_med_time_heading_label, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(main_med_time_heading_label, "TIME");
    }

    if (!main_med_time_label) {
        main_med_time_label = lv_label_create(ui_Screen3);
        lv_obj_align(main_med_time_label, LV_ALIGN_CENTER, 0, 20);
        lv_obj_set_style_text_font(main_med_time_label, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    if (!main_med_dose_label) {
        main_med_dose_label = lv_label_create(ui_Screen3);
        lv_obj_align(main_med_dose_label, LV_ALIGN_CENTER, 0, 55);
        lv_obj_set_style_text_font(main_med_dose_label, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(main_med_dose_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    if (!swipe_hint_label) {
        swipe_hint_label = lv_label_create(ui_Screen3);
        lv_label_set_text(swipe_hint_label, LV_SYMBOL_UP);
        lv_obj_align(swipe_hint_label, LV_ALIGN_BOTTOM_MID, 0, -6);
        lv_obj_set_style_text_font(swipe_hint_label, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    lv_label_set_text(main_med_name_label, "--");
    lv_label_set_text(main_med_time_label, "--:--");
    lv_label_set_text(main_med_dose_label, "Dose: --");
}

static void set_main_data_fetching(void)
{
    if (!main_med_name_label || !main_med_time_label || !main_med_dose_label) {
        return;
    }

    lv_label_set_text(main_med_name_label, "FETCHING...");
    lv_label_set_text(main_med_time_label, "--:--");
    lv_label_set_text(main_med_dose_label, "");
}

static void set_main_data_error(const char *msg)
{
    if (!main_med_name_label || !msg) {
        return;
    }

    lv_label_set_text(main_med_name_label, msg);
    lv_label_set_text(main_med_time_label, "--:--");
    lv_label_set_text(main_med_dose_label, "");
}

static void set_main_data(const char *name, const char *time, const char *dose, const char *status)
{
    (void)status;
    if (!main_med_name_label || !main_med_time_label || !main_med_dose_label) {
        return;
    }

    char buf[96];
    if (name) {
        size_t len = strlen(name);
        if (len >= sizeof(buf)) {
            len = sizeof(buf) - 1;
        }
        for (size_t i = 0; i < len; ++i) {
            char c = name[i];
            if (c >= 'a' && c <= 'z') {
                buf[i] = (char)(c - 32);
            } else {
                buf[i] = c;
            }
        }
        buf[len] = '\0';
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(main_med_name_label, buf);

    lv_snprintf(buf, sizeof(buf), "%s", time ? time : "--:--");
    lv_label_set_text(main_med_time_label, buf);

    lv_snprintf(buf, sizeof(buf), "Dose: %s", dose ? dose : "--");
    lv_label_set_text(main_med_dose_label, buf);
}

static void route_to_screen2(void)
{
    _ui_screen_change(&ui_Screen2, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, ui_Screen2_screen_init);
}

static void route_to_screen3(void)
{
    _ui_screen_change(&ui_Screen3, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, ui_Screen3_screen_init);
}

static void route_to_screen4(void)
{
    _ui_screen_change(&ui_Screen4, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, ui_Screen4_screen_init);
}

static void route_to_wifi_list(void)
{
    if (!wifi_list_screen_get()) {
        wifi_list_screen_init();
    }
    lv_scr_load_anim(wifi_list_screen_get(), LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
    wifi_list_screen_set_status_text("Scanning...");
    wifi_start_scan();
}

static void route_to_wifi(void)
{
    _ui_screen_change(&ui_WifiScreen, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, ui_WifiScreen_screen_init);
    if (ui_Keyboard1 && ui_TextArea2) {
        _ui_keyboard_set_target(ui_Keyboard1, ui_TextArea2);
    }
}

static void ensure_qr_screen(void)
{
    if (qr_screen) {
        return;
    }

    qr_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(qr_screen, LV_OBJ_FLAG_SCROLLABLE);

    qr_code = lv_qrcode_create(qr_screen, 120, lv_color_hex(0x000000), lv_color_hex(0xFFFFFF));
    lv_obj_align(qr_code, LV_ALIGN_TOP_MID, 0, 16);
    lv_qrcode_update(qr_code, DEVICE_ID, strlen(DEVICE_ID));

    qr_device_label = lv_label_create(qr_screen);
    lv_label_set_text(qr_device_label, DEVICE_ID);
    lv_obj_align(qr_device_label, LV_ALIGN_TOP_MID, 0, 150);
    lv_obj_set_style_text_font(qr_device_label, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);

    qr_next_btn = lv_btn_create(qr_screen);
    lv_obj_set_size(qr_next_btn, 140, 44);
    lv_obj_align(qr_next_btn, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_add_event_cb(qr_next_btn, on_qr_next_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *next_lbl = lv_label_create(qr_next_btn);
    lv_label_set_text(next_lbl, "Next");
    lv_obj_center(next_lbl);
}

static void show_qr_screen(void)
{
    ensure_qr_screen();
    lv_scr_load_anim(qr_screen, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
}

static void on_qr_next_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        route_to_wifi_list();
    }
}

static void on_button_to_screen2(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        route_to_screen2();
    }
}

static void on_button_to_screen4(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        route_to_screen4();
    }
}

static void route_to_info_screen(const char *title)
{
    if (!info_screen) {
        info_screen = lv_obj_create(NULL);
        lv_obj_clear_flag(info_screen, LV_OBJ_FLAG_SCROLLABLE);

        info_label = lv_label_create(info_screen);
        lv_obj_align(info_label, LV_ALIGN_TOP_MID, 0, 8);
        lv_obj_set_style_text_font(info_label, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

        info_back_btn = lv_btn_create(info_screen);
        lv_obj_set_size(info_back_btn, 80, 32);
        lv_obj_align(info_back_btn, LV_ALIGN_TOP_LEFT, 6, 6);
        lv_obj_t *back_lbl = lv_label_create(info_back_btn);
        lv_label_set_text(back_lbl, "Back");
        lv_obj_center(back_lbl);
        lv_obj_add_event_cb(info_back_btn, on_info_back_clicked, LV_EVENT_CLICKED, NULL);

        info_list = lv_list_create(info_screen);
        lv_obj_set_size(info_list, 280, 180);
        lv_obj_align(info_list, LV_ALIGN_BOTTOM_MID, 0, -8);
        lv_obj_set_scrollbar_mode(info_list, LV_SCROLLBAR_MODE_AUTO);
    }

    if (info_label && title) {
        lv_label_set_text(info_label, title);
    }
    lv_scr_load_anim(info_screen, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
}

static void motor_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << MOTOR_IN1_GPIO) | (1ULL << MOTOR_IN2_GPIO) |
                        (1ULL << MOTOR_IN3_GPIO) | (1ULL << MOTOR_IN4_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&cfg);

    gpio_set_level(MOTOR_IN1_GPIO, 0);
    gpio_set_level(MOTOR_IN2_GPIO, 0);
    gpio_set_level(MOTOR_IN3_GPIO, 0);
    gpio_set_level(MOTOR_IN4_GPIO, 0);
    motor_step_index = 0;
}

static void motor_set_phase(int index)
{
    static const uint8_t phase[4][4] = {
        {1, 0, 0, 0},
        {0, 0, 1, 0},
        {0, 1, 0, 0},
        {0, 0, 0, 1}
    };
    int idx = index & 0x03;
    gpio_set_level(MOTOR_IN1_GPIO, phase[idx][0]);
    gpio_set_level(MOTOR_IN3_GPIO, phase[idx][1]);
    gpio_set_level(MOTOR_IN2_GPIO, phase[idx][2]);
    gpio_set_level(MOTOR_IN4_GPIO, phase[idx][3]);
}

static void motor_move_steps(int steps)
{
    if (steps == 0) {
        return;
    }
    int dir = (steps > 0) ? 1 : -1;
    int count = steps > 0 ? steps : -steps;
    for (int i = 0; i < count; ++i) {
        motor_step_index = (motor_step_index + dir + 4) % 4;
        motor_set_phase(motor_step_index);
        esp_rom_delay_us(CALIBRATE_STEP_DELAY_US);
    }
}

static void stepper_move_to_slot(int slot)
{
    if (slot < 1) {
        ESP_LOGW(TAG, "Stepper slot too low (%d); using slot 1", slot);
        slot = 1;
    } else if (slot > STEPPER_TOTAL_SLOTS) {
        ESP_LOGW(TAG, "Stepper slot too high (%d); using slot %d", slot, STEPPER_TOTAL_SLOTS);
        slot = STEPPER_TOTAL_SLOTS;
    }

    int target_steps = STEPPER_STEPS_PER_SLOT * (slot - 1);
    int delta = target_steps - stepper_current_steps;
    if (delta == 0) {
        return;
    }

    ESP_LOGI(TAG, "Stepper moving to slot %d (delta=%d)", slot, delta);
    motor_move_steps(delta);
    stepper_current_steps = target_steps;
    stepper_current_slot = slot;
    stepper_slot_save(slot);
}

static void stepper_slot_load(void)
{
    nvs_handle_t handle;
    if (nvs_open("doseright", NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    int32_t stored_slot = 0;
    if (nvs_get_i32(handle, "stepper_slot", &stored_slot) == ESP_OK) {
        if (stored_slot >= 1 && stored_slot <= STEPPER_TOTAL_SLOTS) {
            stepper_current_slot = (int)stored_slot;
            stepper_current_steps = STEPPER_STEPS_PER_SLOT * (stepper_current_slot - 1);
        }
    }
    nvs_close(handle);
}

static void stepper_slot_save(int slot)
{
    nvs_handle_t handle;
    if (nvs_open("doseright", NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    nvs_set_i32(handle, "stepper_slot", (int32_t)slot);
    nvs_commit(handle);
    nvs_close(handle);
}

static void servo_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode = SERVO_LEDC_MODE,
        .duty_resolution = SERVO_LEDC_RESOLUTION,
        .timer_num = SERVO_LEDC_TIMER,
        .freq_hz = 50,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer_cfg);

    ledc_channel_config_t ch_cfg = {
        .gpio_num = SERVO_GPIO,
        .speed_mode = SERVO_LEDC_MODE,
        .channel = SERVO_LEDC_CHANNEL,
        .timer_sel = SERVO_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&ch_cfg);
}

static void servo_set_pulse_us(uint32_t pulse_us)
{
    const uint32_t max_duty = (1u << SERVO_LEDC_RESOLUTION) - 1u;
    uint32_t duty = (pulse_us * max_duty) / SERVO_PERIOD_US;
    ledc_set_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL, duty);
    ledc_update_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL);
}

static void servo_set_degree(int degree)
{
    if (degree < 0) {
        degree = 0;
    }
    if (degree > SERVO_DEG_MAX) {
        degree = SERVO_DEG_MAX;
    }
    uint32_t pulse = SERVO_MIN_US +
        (uint32_t)((SERVO_MAX_US - SERVO_MIN_US) * degree / SERVO_DEG_MAX);
    servo_set_pulse_us(pulse);
}

static void servo_test_timer_cb(void *arg)
{
    (void)arg;
    if (servo_deg > SERVO_SWEEP_MAX_DEG) {
        servo_deg = SERVO_SWEEP_MAX_DEG;
    }
    if (servo_deg < 0) {
        servo_deg = 0;
    }
    servo_set_degree(servo_deg);
    servo_deg += SERVO_STEP_DEG;
    if (servo_deg > SERVO_SWEEP_MAX_DEG) {
        servo_deg = 0;
    }
}

static void servo_test_start(void)
{
    if (servo_timer) {
        return;
    }
    servo_init();
    servo_left = true;
    servo_deg = 0;
    servo_set_degree(0);

    const esp_timer_create_args_t args = {
        .callback = &servo_test_timer_cb,
        .name = "servo_test"
    };
    if (esp_timer_create(&args, &servo_timer) == ESP_OK) {
        esp_timer_start_periodic(servo_timer, SERVO_STEP_INTERVAL_US);
    }
}

static void servo_test_stop(void)
{
    if (servo_timer) {
        esp_timer_stop(servo_timer);
    }
    servo_current_deg = servo_deg;
    if (servo_current_deg < 0) {
        servo_current_deg = 0;
    }
    if (servo_current_deg > SERVO_DEG_MAX) {
        servo_current_deg = SERVO_DEG_MAX;
    }
}

static void ir_sensor_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << IR_SENSOR_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&cfg);
}

static void ir_sensor_timer_cb(void *arg)
{
    (void)arg;
    if (!ir_enabled) {
        return;
    }
    int state = gpio_get_level(IR_SENSOR_GPIO);
    if (state == last_ir_state) {
        return;
    }
    last_ir_state = state;
    if (state == 0) {
        ESP_LOGI(TAG, "IR: object detected");
        ir_detected_once = true;
    } else {
        ESP_LOGI(TAG, "IR: object not detected");
        if (ir_detected_once && !ir_close_pending) {
            ir_close_pending = true;
            ir_sensor_set_enabled(false);
            if (!ir_close_timer) {
                const esp_timer_create_args_t args = {
                    .callback = &ir_close_timer_cb,
                    .name = "ir_close"
                };
                if (esp_timer_create(&args, &ir_close_timer) != ESP_OK) {
                    return;
                }
            }
            esp_timer_start_once(ir_close_timer, 2000000);
        }
    }
}

static void ir_sensor_set_enabled(bool enabled)
{
    ir_enabled = enabled;
    last_ir_state = -1;
    if (enabled) {
        ir_close_pending = false;
        ir_detected_once = false;
    }
}

static void ir_close_timer_cb(void *arg)
{
    (void)arg;
    servo_start_move(180, false);
}

static int get_wifi_strength(void)
{
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
        return 0;
    }
    return ap_info.rssi;
}

static int get_battery_level(void)
{
    return DEMO_BATTERY_LEVEL;
}

static int get_uptime_seconds(void)
{
    return (int)(esp_timer_get_time() / 1000000);
}

static int get_storage_free_kb(void)
{
    return DEMO_STORAGE_FREE_KB;
}

static float get_temperature_c(void)
{
    return DEMO_TEMPERATURE_C;
}

static void log_wifi_status(const char *context)
{
    int rssi = get_wifi_strength();
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        ESP_LOGW(TAG, "%s wifi status: rssi=%d, ip=unknown", context ? context : "wifi", rssi);
        return;
    }

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) {
        ESP_LOGW(TAG, "%s wifi status: rssi=%d, ip=unknown", context ? context : "wifi", rssi);
        return;
    }

    ESP_LOGW(TAG, "%s wifi status: rssi=%d, ip=" IPSTR, context ? context : "wifi", rssi, IP2STR(&ip_info.ip));
}

static bool send_heartbeat(void)
{
    if (!wifi_is_connected()) {
        return false;
    }

    char url[256];
    snprintf(url, sizeof(url), "%s/api/hardware/heartbeat", BACKEND_BASE_URL);
    ESP_LOGI(TAG, "HTTP POST %s", url);

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return false;
    }
    cJSON_AddStringToObject(root, "deviceId", DEVICE_ID);
    cJSON_AddNumberToObject(root, "batteryLevel", get_battery_level());
    cJSON_AddNumberToObject(root, "wifiStrength", get_wifi_strength());
    cJSON_AddStringToObject(root, "status", "online");
    cJSON_AddStringToObject(root, "firmwareVersion", FIRMWARE_VERSION);
    cJSON_AddNumberToObject(root, "uptimeSeconds", get_uptime_seconds());
    cJSON_AddNumberToObject(root, "storageFreeKb", get_storage_free_kb());
    cJSON_AddNumberToObject(root, "temperatureC", get_temperature_c());
    cJSON_AddNullToObject(root, "lastError");
    cJSON_AddNumberToObject(root, "slotCount", STEPPER_TOTAL_SLOTS);

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) {
        return false;
    }

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 5000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(body);
        return false;
    }

    char auth_header[128];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", DEVICE_SECRET);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, (int)strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Heartbeat perform failed: %s", esp_err_to_name(err));
        log_wifi_status("heartbeat");
        esp_http_client_cleanup(client);
        free(body);
        return false;
    }

    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(body);
    ESP_LOGI(TAG, "Heartbeat status: %d", status);
    return status >= 200 && status < 300;
}

static void heartbeat_task(void *arg)
{
    (void)arg;
    while (true) {
        send_heartbeat();
        vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_INTERVAL_MS));
    }
}

typedef struct {
    char dose_id[40];
    bool taken;
} dose_event_t;

static bool send_dose_event(const char *dose_id, bool taken)
{
    if (!dose_id || dose_id[0] == '\0') {
        ESP_LOGE(TAG, "Dose event failed: missing doseId");
        return false;
    }
    if (!wifi_is_connected()) {
        ESP_LOGE(TAG, "Dose event failed: WiFi not connected");
        return false;
    }

    char url[256];
    const char *action = taken ? "mark-taken" : "mark-skipped";
    snprintf(url, sizeof(url), "%s/api/hardware/doses/%s/%s", BACKEND_BASE_URL, dose_id,
             action);
    ESP_LOGI(TAG, "HTTP PATCH %s", url);

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return false;
    }
    cJSON_AddStringToObject(root, "deviceId", DEVICE_ID);
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) {
        return false;
    }

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 5000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(body);
        return false;
    }

    char auth_header[128];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", DEVICE_SECRET);
    esp_http_client_set_method(client, HTTP_METHOD_PATCH);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, (int)strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Dose event perform failed: %s", esp_err_to_name(err));
        log_wifi_status("dose_event");
        esp_http_client_cleanup(client);
        free(body);
        return false;
    }

    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(body);
    bool ok = status >= 200 && status < 300;
    if (ok) {
        ESP_LOGI(TAG, "Dose event %s success (status=%d)", action, status);
    } else {
        ESP_LOGE(TAG, "Dose event %s failed (status=%d)", action, status);
    }
    return ok;
}

static void send_dose_event_async(bool taken)
{
    if (current_alert_dose_id[0] == '\0') {
        ESP_LOGW(TAG, "Dose event skipped: missing doseId");
        return;
    }
    dose_event_t *evt = (dose_event_t *)calloc(1, sizeof(dose_event_t));
    if (!evt) {
        return;
    }
    snprintf(evt->dose_id, sizeof(evt->dose_id), "%s", current_alert_dose_id);
    evt->taken = taken;
    xTaskCreate(dose_event_task, "dose_event", 4096, evt, 5, NULL);
}

static void dose_event_task(void *arg)
{
    dose_event_t *data = (dose_event_t *)arg;
    if (data) {
        send_dose_event(data->dose_id, data->taken);
        free(data);
    }
    vTaskDelete(NULL);
}

static void ir_sensor_start(void)
{
    if (ir_timer) {
        return;
    }
    ir_sensor_init();
    last_ir_state = -1;

    const esp_timer_create_args_t args = {
        .callback = &ir_sensor_timer_cb,
        .name = "ir_sensor"
    };
    if (esp_timer_create(&args, &ir_timer) == ESP_OK) {
        esp_timer_start_periodic(ir_timer, IR_POLL_INTERVAL_US);
    }
}

static void show_info_screen(const char *title)
{
    route_to_info_screen(title);
    if (info_list) {
        lv_obj_clean(info_list);
    }
}

static void queue_info_fetch(const char *path, const char *title)
{
    if (!path || !title) {
        return;
    }
    snprintf(pending_info_path, sizeof(pending_info_path), "%s", path);
    snprintf(pending_info_title, sizeof(pending_info_title), "%s", title);
    pending_info_fetch = true;
}

static void add_info_line(const char *text)
{
    if (!info_list || !text) {
        return;
    }
    lv_list_add_text(info_list, text);
}

static med_cache_t *get_cache_for_path(const char *path)
{
    if (!path) {
        return NULL;
    }
    if (strcmp(path, "/api/hardware/taken") == 0) {
        return &cache_taken;
    }
    if (strcmp(path, "/api/hardware/upcoming") == 0) {
        return &cache_upcoming;
    }
    if (strcmp(path, "/api/hardware/missed") == 0) {
        return &cache_missed;
    }
    return NULL;
}

static void med_cache_set_updated(med_cache_t *cache)
{
    if (!cache) {
        return;
    }
    if (strcmp(time_display, "--:--") != 0) {
        snprintf(cache->updated, sizeof(cache->updated), "%s", time_display);
    } else {
        snprintf(cache->updated, sizeof(cache->updated), "Unknown");
    }
}

static void render_med_cache(const char *title, const med_cache_t *cache, bool offline)
{
    show_info_screen(title);
    if (!cache || !cache->valid) {
        add_info_line("No cached data");
        return;
    }

    if (offline) {
        add_info_line("Offline - showing last data");
    }
    if (cache->updated[0] != '\0') {
        char line[48];
        snprintf(line, sizeof(line), "Last update: %s", cache->updated);
        add_info_line(line);
    }

    if (cache->count == 0) {
        add_info_line("No records found");
        return;
    }

    for (size_t i = 0; i < cache->count; ++i) {
        const med_cache_item_t *item = &cache->items[i];
        char line[128];
        char time_buf[16];
        format_time_12h(item->time_str, time_buf, sizeof(time_buf));
        snprintf(line, sizeof(line), "%s  |  %s", item->name[0] ? item->name : "--",
             time_buf[0] ? time_buf : "--:--");
        add_info_line(line);

        snprintf(line, sizeof(line), "Dose: %s  Slot: %d", item->dose[0] ? item->dose : "--", item->slot);
        add_info_line(line);
        add_info_line(" ");
    }
}

static bool apply_cached_upcoming_to_main(void)
{
    ensure_med_cache_loaded("/api/hardware/upcoming", &cache_upcoming);
    if (!cache_upcoming.valid) {
        return false;
    }
    if (cache_upcoming.count == 0) {
        set_main_data_error("No cached meds");
        return true;
    }

    const med_cache_item_t *item = &cache_upcoming.items[0];
    set_main_data(item->name, item->time_str, item->dose, item->status);
    return true;
}

static void fetch_and_show_meds(const char *path, const char *title)
{
    med_cache_t *cache = get_cache_for_path(path);
    const char *cache_key = get_cache_key_for_path(path);
    ensure_med_cache_loaded(path, cache);
    snprintf(current_info_path, sizeof(current_info_path), "%s", path ? path : "");
    snprintf(current_info_title, sizeof(current_info_title), "%s", title ? title : "");
    if (cache && cache->valid) {
        render_med_cache(title, cache, false);
    } else {
        show_info_screen(title);
        add_info_line("Loading...");
    }

    if (!wifi_is_connected()) {
        if (cache && cache->valid) {
            render_med_cache(title, cache, true);
        } else if (info_list) {
            lv_obj_clean(info_list);
            add_info_line("WiFi not connected");
            add_info_line("Connect to WiFi and try again");
        }
        return;
    }
    if (cache_key) {
        queue_info_fetch(path, title);
    }
}

static void on_info_back_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        show_menu_screen();
    }
}

static void on_calibrate_back_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        show_calibrate_menu_screen();
    }
}

static void on_calibrate_menu_back_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        show_menu_screen();
    }
}

static void on_stepper_option_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        show_calibrate_screen();
    }
}

static void on_servo_option_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        show_servo_calibrate_screen();
    }
}

static void on_calibrate_move_timer(lv_timer_t *timer)
{
    (void)timer;
    if (calibrate_move_dir == 0) {
        return;
    }
    motor_move_steps(calibrate_move_dir * CALIBRATE_CONT_STEP);
}

static void on_calibrate_move_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    int dir = (int)(intptr_t)lv_event_get_user_data(e);

    if (code == LV_EVENT_PRESSED) {
        calibrate_move_dir = dir;
        if (!calibrate_move_timer) {
            calibrate_move_timer = lv_timer_create(on_calibrate_move_timer, CALIBRATE_CONT_INTERVAL_MS, NULL);
        } else {
            lv_timer_resume(calibrate_move_timer);
        }
        if (calibrate_status_label) {
            lv_label_set_text(calibrate_status_label, dir < 0 ? "Moving left" : "Moving right");
        }
        return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        calibrate_move_dir = 0;
        if (calibrate_move_timer) {
            lv_timer_pause(calibrate_move_timer);
        }
        if (calibrate_status_label) {
            lv_label_set_text(calibrate_status_label, "Stopped");
        }
    }
}

static void servo_move_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (servo_current_deg == servo_target_deg) {
        if (servo_move_timer) {
            lv_timer_del(servo_move_timer);
            servo_move_timer = NULL;
        }
        if (pending_mark_taken && servo_target_deg == 180) {
            pending_mark_taken = false;
            send_dose_event_async(true);
        }
        if (servo_status_label) {
            char buf[48];
            snprintf(buf, sizeof(buf), "Reached %d deg", servo_target_deg);
            lv_label_set_text(servo_status_label, buf);
        }
        if (servo_enable_ir_on_complete) {
            servo_enable_ir_on_complete = false;
            ir_sensor_set_enabled(true);
        }
        return;
    }

    if (servo_current_deg < servo_target_deg) {
        servo_current_deg += SERVO_STEP_DEG;
        if (servo_current_deg > servo_target_deg) {
            servo_current_deg = servo_target_deg;
        }
    } else {
        servo_current_deg -= SERVO_STEP_DEG;
        if (servo_current_deg < servo_target_deg) {
            servo_current_deg = servo_target_deg;
        }
    }
    servo_set_degree(servo_current_deg);
}

static void servo_start_move(int target_deg, bool enable_ir_on_complete)
{
    servo_target_deg = target_deg;
    servo_enable_ir_on_complete = enable_ir_on_complete;

    if (servo_move_timer) {
        lv_timer_del(servo_move_timer);
        servo_move_timer = NULL;
    }
    servo_move_timer = lv_timer_create(servo_move_timer_cb, SERVO_STEP_INTERVAL_US / 1000, NULL);
}

static void ensure_calibrate_screen(void)
{
    if (calibrate_screen) {
        return;
    }

    calibrate_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(calibrate_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(calibrate_screen);
    lv_label_set_text(title, "Calibrate");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *hint = lv_label_create(calibrate_screen);
    lv_label_set_text(hint, "Press and hold to calibrate");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *back_btn = lv_btn_create(calibrate_screen);
    lv_obj_set_size(back_btn, 34, 28);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 6, 6);
    lv_obj_add_event_cb(back_btn, on_calibrate_back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
    lv_obj_center(back_lbl);

    lv_obj_t *left_btn = lv_btn_create(calibrate_screen);
    lv_obj_set_size(left_btn, 130, 90);
    lv_obj_align(left_btn, LV_ALIGN_CENTER, -70, 0);
    lv_obj_add_event_cb(left_btn, on_calibrate_move_event, LV_EVENT_ALL, (void *)(intptr_t)-1);
    lv_obj_t *left_lbl = lv_label_create(left_btn);
    lv_label_set_text(left_lbl, "Anti-clockwise");
    lv_obj_center(left_lbl);

    lv_obj_t *right_btn = lv_btn_create(calibrate_screen);
    lv_obj_set_size(right_btn, 130, 90);
    lv_obj_align(right_btn, LV_ALIGN_CENTER, 70, 0);
    lv_obj_add_event_cb(right_btn, on_calibrate_move_event, LV_EVENT_ALL, (void *)(intptr_t)1);
    lv_obj_t *right_lbl = lv_label_create(right_btn);
    lv_label_set_text(right_lbl, "Clockwise");
    lv_obj_center(right_lbl);

    calibrate_status_label = lv_label_create(calibrate_screen);
    lv_label_set_text(calibrate_status_label, "Tap Left/Right");
    lv_obj_align(calibrate_status_label, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_text_font(calibrate_status_label, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void ensure_calibrate_menu_screen(void)
{
    if (calibrate_menu_screen) {
        return;
    }

    calibrate_menu_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(calibrate_menu_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(calibrate_menu_screen);
    lv_label_set_text(title, "Calibrate");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *back_btn = lv_btn_create(calibrate_menu_screen);
    lv_obj_set_size(back_btn, 34, 28);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 6, 6);
    lv_obj_add_event_cb(back_btn, on_calibrate_menu_back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
    lv_obj_center(back_lbl);

    lv_obj_t *stepper_btn = lv_btn_create(calibrate_menu_screen);
    lv_obj_set_size(stepper_btn, 200, 50);
    lv_obj_align(stepper_btn, LV_ALIGN_CENTER, 0, -20);
    lv_obj_add_event_cb(stepper_btn, on_stepper_option_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *stepper_lbl = lv_label_create(stepper_btn);
    lv_label_set_text(stepper_lbl, "Stepper");
    lv_obj_center(stepper_lbl);

    lv_obj_t *servo_btn = lv_btn_create(calibrate_menu_screen);
    lv_obj_set_size(servo_btn, 200, 50);
    lv_obj_align(servo_btn, LV_ALIGN_CENTER, 0, 40);
    lv_obj_add_event_cb(servo_btn, on_servo_option_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *servo_lbl = lv_label_create(servo_btn);
    lv_label_set_text(servo_lbl, "Servo");
    lv_obj_center(servo_lbl);
}

static void show_calibrate_menu_screen(void)
{
    ensure_calibrate_menu_screen();
    lv_scr_load_anim(calibrate_menu_screen, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
}

static void ensure_servo_calibrate_screen(void)
{
    if (servo_screen) {
        return;
    }

    servo_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(servo_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(servo_screen);
    lv_label_set_text(title, "Servo");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *back_btn = lv_btn_create(servo_screen);
    lv_obj_set_size(back_btn, 34, 28);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 6, 6);
    lv_obj_add_event_cb(back_btn, on_servo_back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
    lv_obj_center(back_lbl);

    servo_slider = lv_slider_create(servo_screen);
    lv_obj_set_size(servo_slider, 220, 14);
    lv_obj_align(servo_slider, LV_ALIGN_CENTER, 0, -10);
    lv_slider_set_range(servo_slider, 80, 180);
    if (servo_current_deg < 80) {
        servo_current_deg = 80;
    }
    if (servo_current_deg > 180) {
        servo_current_deg = 180;
    }
    lv_slider_set_value(servo_slider, servo_current_deg, LV_ANIM_OFF);
    lv_obj_add_event_cb(servo_slider, on_servo_slider_changed, LV_EVENT_VALUE_CHANGED, NULL);

    servo_value_label = lv_label_create(servo_screen);
    lv_obj_align(servo_value_label, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_text_font(servo_value_label, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "Target: %d deg", servo_current_deg);
        lv_label_set_text(servo_value_label, buf);
    }

    lv_obj_t *move_btn = lv_btn_create(servo_screen);
    lv_obj_set_size(move_btn, 120, 40);
    lv_obj_align(move_btn, LV_ALIGN_CENTER, 0, 50);
    lv_obj_add_event_cb(move_btn, on_servo_move_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *move_lbl = lv_label_create(move_btn);
    lv_label_set_text(move_lbl, "Move");
    lv_obj_center(move_lbl);

    servo_status_label = lv_label_create(servo_screen);
    lv_obj_align(servo_status_label, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_set_style_text_font(servo_status_label, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(servo_status_label, "Ready");
}

static void show_servo_calibrate_screen(void)
{
    servo_test_stop();
    servo_init();
    ensure_servo_calibrate_screen();
    if (servo_slider) {
        if (servo_current_deg < 80) {
            servo_current_deg = 80;
        }
        if (servo_current_deg > 180) {
            servo_current_deg = 180;
        }
        lv_slider_set_value(servo_slider, servo_current_deg, LV_ANIM_OFF);
    }
    lv_scr_load_anim(servo_screen, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
}

static void on_servo_back_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (servo_move_timer) {
        lv_timer_del(servo_move_timer);
        servo_move_timer = NULL;
    }
    show_calibrate_menu_screen();
}

static void on_servo_slider_changed(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED || !servo_slider || !servo_value_label) {
        return;
    }
    int value = lv_slider_get_value(servo_slider);
    servo_target_deg = value;
    char buf[32];
    snprintf(buf, sizeof(buf), "Target: %d deg", value);
    lv_label_set_text(servo_value_label, buf);
}

static void on_servo_move_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED || !servo_slider) {
        return;
    }
    servo_target_deg = lv_slider_get_value(servo_slider);
    if (servo_current_deg < 80) {
        servo_current_deg = 80;
    }
    if (servo_current_deg > 180) {
        servo_current_deg = 180;
    }
    servo_set_degree(servo_current_deg);

    if (servo_status_label) {
        char buf[48];
        snprintf(buf, sizeof(buf), "Moving to %d deg", servo_target_deg);
        lv_label_set_text(servo_status_label, buf);
    }

    servo_start_move(servo_target_deg, false);
}

static void show_calibrate_screen(void)
{
    ensure_calibrate_screen();
    lv_scr_load_anim(calibrate_screen, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
}

static void show_refill_screen(void);

static void on_main_menu_selected(main_menu_item_t item)
{
    switch (item) {
        case MAIN_MENU_TAKEN:
            fetch_and_show_meds("/api/hardware/taken", "TAKEN");
            break;
        case MAIN_MENU_UPCOMING:
            fetch_and_show_meds("/api/hardware/upcoming", "UPCOMING");
            break;
        case MAIN_MENU_MISSED:
            fetch_and_show_meds("/api/hardware/missed", "MISSED");
            break;
        case MAIN_MENU_REFILL:
            show_refill_screen();
            break;
        case MAIN_MENU_CALIBRATE:
            show_calibrate_menu_screen();
            break;
        case MAIN_MENU_SETTINGS:
            settings_screen_show();
            break;
        case MAIN_MENU_HELP:
            help_screen_show();
            break;
        default:
            break;
    }
}

static void ensure_menu_screen(void)
{
    if (!menu_screen) {
        menu_screen = lv_obj_create(NULL);
        lv_obj_clear_flag(menu_screen, LV_OBJ_FLAG_SCROLLABLE);
        main_menu_screen_init(menu_screen);
        main_menu_screen_set_on_select(on_main_menu_selected);
    }
}

static void show_menu_screen(void)
{
    ensure_menu_screen();
    lv_scr_load_anim(menu_screen, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
}

static void on_refill_back_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    show_menu_screen();
}

static void on_refill_slot_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    if (slot < 1 || slot > STEPPER_TOTAL_SLOTS) {
        return;
    }

    if (refill_status_label) {
        char buf[48];
        snprintf(buf, sizeof(buf), "Opening compartment %d", slot);
        lv_label_set_text(refill_status_label, buf);
    }

    stepper_move_to_slot(slot);

    servo_test_stop();
    servo_init();
    ir_sensor_set_enabled(false);
    ir_close_pending = false;
    ir_detected_once = false;
    pending_mark_taken = false;

    servo_current_deg = 180;
    servo_set_degree(servo_current_deg);
    servo_start_move(80, true);
}

static void ensure_refill_screen(void)
{
    if (refill_screen) {
        return;
    }

    refill_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(refill_screen, LV_OBJ_FLAG_SCROLLABLE);

    refill_title_label = lv_label_create(refill_screen);
    lv_label_set_text(refill_title_label, "REFILL");
    lv_obj_align(refill_title_label, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_text_font(refill_title_label, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

    refill_back_btn = lv_btn_create(refill_screen);
    lv_obj_set_size(refill_back_btn, 34, 28);
    lv_obj_align(refill_back_btn, LV_ALIGN_TOP_LEFT, 6, 6);
    lv_obj_add_event_cb(refill_back_btn, on_refill_back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(refill_back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
    lv_obj_center(back_lbl);

    refill_list = lv_obj_create(refill_screen);
    lv_obj_set_size(refill_list, 260, 170);
    lv_obj_align(refill_list, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_radius(refill_list, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(refill_list, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(refill_list, lv_color_hex(0xD0D5DD), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(refill_list, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(refill_list, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(refill_list, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(refill_list, LV_FLEX_FLOW_COLUMN);

    for (int i = 0; i < STEPPER_TOTAL_SLOTS; ++i) {
        refill_buttons[i] = lv_btn_create(refill_list);
        lv_obj_set_width(refill_buttons[i], 240);
        lv_obj_set_height(refill_buttons[i], 26);
        lv_obj_add_event_cb(refill_buttons[i], on_refill_slot_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)(i + 1));

        lv_obj_t *label = lv_label_create(refill_buttons[i]);
        char text[32];
        snprintf(text, sizeof(text), "Compartment %d", i + 1);
        lv_label_set_text(label, text);
        lv_obj_center(label);
    }

    refill_status_label = lv_label_create(refill_screen);
    lv_label_set_text(refill_status_label, "Select a compartment");
    lv_obj_align(refill_status_label, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_set_style_text_font(refill_status_label, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(refill_status_label, lv_color_hex(0x667085), LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void show_refill_screen(void)
{
    ensure_refill_screen();
    lv_scr_load_anim(refill_screen, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
}

static void on_main_menu_back(void)
{
    route_to_screen3();
}

static void on_submenu_back(void)
{
    show_menu_screen();
}

static void on_alert_pick_action(void)
{
    servo_test_stop();
    servo_init();
    ir_sensor_set_enabled(false);
    ir_close_pending = false;
    ir_detected_once = false;
    if (current_alert_dose_id[0] == '\0') {
        ESP_LOGW(TAG, "Pick pressed but doseId is missing; mark-taken will be skipped");
        pending_mark_taken = false;
    } else {
        pending_mark_taken = true;
    }
    if (ir_close_timer) {
        esp_timer_stop(ir_close_timer);
    }
    servo_current_deg = 180;
    servo_set_degree(servo_current_deg);
    if (servo_status_label) {
        lv_label_set_text(servo_status_label, "Opening lid...");
    }
    servo_start_move(80, true);
    route_to_screen3();
}

static void on_alert_skip_action(void)
{
    pending_mark_taken = false;
    send_dose_event_async(false);
    route_to_screen3();
}

static void on_main_screen_gesture(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) {
        return;
    }
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) {
        return;
    }
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_TOP) {
        show_menu_screen();
    }
}

static void on_button_to_wifi(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        route_to_wifi_list();
    }
}

static void on_wifi_logo_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        route_to_wifi_list();
    }
}

static void loading_timer_cb(lv_timer_t *timer)
{
    lv_timer_del(timer);
    if (boot_progress_timer) {
        lv_timer_del(boot_progress_timer);
        boot_progress_timer = NULL;
    }
    if (wifi_is_connected()) {
        route_to_screen3();
    } else {
        show_qr_screen();
    }
}

static void boot_progress_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!ui_Bar1) {
        return;
    }
    boot_progress_value += 2;
    if (boot_progress_value > 100) {
        boot_progress_value = 0;
    }
    lv_bar_set_value(ui_Bar1, boot_progress_value, LV_ANIM_OFF);
}

static void on_main_button_click(void *btn, void *arg)
{
    (void)btn;
    (void)arg;
    lvgl_port_lock(0);
    route_to_screen3();
    lvgl_port_unlock();
}

static void init_main_button(void)
{
    button_handle_t btn_array[BSP_BUTTON_NUM] = {0};
    int btn_cnt = 0;
    if (bsp_iot_button_create(btn_array, &btn_cnt, BSP_BUTTON_NUM) != ESP_OK) {
        return;
    }
    if (BSP_BUTTON_MAIN < btn_cnt) {
        main_button = btn_array[BSP_BUTTON_MAIN];
        if (main_button) {
            iot_button_register_cb(main_button, BUTTON_SINGLE_CLICK, on_main_button_click, NULL);
        }
    }
}

static void wifi_start_scan(void)
{
    if (!wifi_ready) {
        return;
    }

    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true
    };

    esp_wifi_scan_start(&scan_cfg, false);
}

static bool wifi_is_connected(void)
{
    wifi_ap_record_t ap_info;
    return esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK;
}

static bool backend_fetch_upcoming(void)
{
    char url[256];
    snprintf(url, sizeof(url), "%s/api/hardware/upcoming?deviceId=%s", BACKEND_BASE_URL, DEVICE_ID);
    ESP_LOGI(TAG, "Fetching: %s", url);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 5000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "HTTP client init failed");
        snprintf(backend_last_error, sizeof(backend_last_error), "HTTP init failed");
        return false;
    }

    char auth_header[128];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", DEVICE_SECRET);
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    esp_http_client_set_header(client, "Authorization", auth_header);

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        snprintf(backend_last_error, sizeof(backend_last_error), "HTTP open: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int buffer_size = (content_length > 0) ? (content_length + 1) : 4096;
    char *buffer = (char *)malloc(buffer_size);
    if (!buffer) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    int total_read = 0;
    int read_len = 0;
    while ((read_len = esp_http_client_read(client, buffer + total_read, buffer_size - 1 - total_read)) > 0) {
        total_read += read_len;
        if (total_read >= buffer_size - 1) {
            buffer_size *= 2;
            char *new_buf = (char *)realloc(buffer, buffer_size);
            if (!new_buf) {
                free(buffer);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return false;
            }
            buffer = new_buf;
        }
    }
    buffer[total_read] = '\0';

    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "HTTP status: %d, bytes: %d", status, total_read);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    log_http_response("upcoming", url, status, buffer, total_read);

    if (status != 200 || total_read == 0) {
        ESP_LOGE(TAG, "HTTP error status=%d, bytes=%d", status, total_read);
        snprintf(backend_last_error, sizeof(backend_last_error), "HTTP %d", status);
        free(buffer);
        return false;
    }

    cJSON *root = cJSON_Parse(buffer);
    free(buffer);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse failed");
        snprintf(backend_last_error, sizeof(backend_last_error), "JSON parse failed");
        return false;
    }

    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsArray(data) || cJSON_GetArraySize(data) == 0) {
        cJSON_Delete(root);
        lvgl_port_lock(0);
        set_main_data_error("No upcoming meds");
        lvgl_port_unlock();
        return true;
    }

    cache_upcoming.count = 0;
    int count = cJSON_GetArraySize(data);
    if (count > MED_CACHE_MAX) {
        count = MED_CACHE_MAX;
    }
    for (int i = 0; i < count; ++i) {
        cJSON *item = cJSON_GetArrayItem(data, i);
        const char *name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "medicineName"));
        const char *dose = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "dosage"));
        const char *time = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "scheduledTime"));
        const char *status = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "status"));
        const char *dose_id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "doseId"));
        if (!dose_id) {
            dose_id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "id"));
        }
        if (!dose_id) {
            ESP_LOGW(TAG, "Upcoming item missing doseId (name=%s, time=%s)",
                     name ? name : "?", time ? time : "?");
        }
        int slot = cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(item, "slot"))
            ? cJSON_GetObjectItemCaseSensitive(item, "slot")->valueint
            : 0;

        med_cache_item_t *dst = &cache_upcoming.items[i];
        snprintf(dst->name, sizeof(dst->name), "%s", name ? name : "");
        snprintf(dst->dose, sizeof(dst->dose), "%s", dose ? dose : "");
        snprintf(dst->time_str, sizeof(dst->time_str), "%s", time ? time : "");
        snprintf(dst->status, sizeof(dst->status), "%s", status ? status : "");
        snprintf(dst->dose_id, sizeof(dst->dose_id), "%s", dose_id ? dose_id : "");
        dst->slot = slot;
        cache_upcoming.count++;
    }
    cache_upcoming.valid = true;
    med_cache_set_updated(&cache_upcoming);
    med_cache_save_nvs("med_upcoming", &cache_upcoming);

    cJSON *item = cJSON_GetArrayItem(data, 0);
    const char *name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "medicineName"));
    const char *dose = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "dosage"));
    const char *time = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "scheduledTime"));
    const char *status_txt = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "status"));
    const char *dose_id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "doseId"));
    if (!dose_id) {
        dose_id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "id"));
    }

    lvgl_port_lock(0);
    char time_buf[16];
    format_time_12h(time, time_buf, sizeof(time_buf));
    set_main_data(name, time_buf, dose, status_txt);
    lvgl_port_unlock();

    snprintf(current_alert_dose_id, sizeof(current_alert_dose_id), "%s", dose_id ? dose_id : "");

    cJSON_Delete(root);
    return true;
}

static bool backend_fetch_cache(const char *path, med_cache_t *cache, const char *cache_key)
{
    if (!path || !cache || !cache_key) {
        return false;
    }

    char url[256];
    snprintf(url, sizeof(url), "%s%s?deviceId=%s", BACKEND_BASE_URL, path, DEVICE_ID);
    ESP_LOGI(TAG, "HTTP GET %s", url);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 5000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return false;
    }

    char auth_header[128];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", DEVICE_SECRET);
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    esp_http_client_set_header(client, "Authorization", auth_header);

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return false;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int buffer_size = (content_length > 0) ? (content_length + 1) : 4096;
    char *buffer = (char *)malloc(buffer_size);
    if (!buffer) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    int total_read = 0;
    int read_len = 0;
    while ((read_len = esp_http_client_read(client, buffer + total_read, buffer_size - 1 - total_read)) > 0) {
        total_read += read_len;
        if (total_read >= buffer_size - 1) {
            buffer_size *= 2;
            char *new_buf = (char *)realloc(buffer, buffer_size);
            if (!new_buf) {
                free(buffer);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return false;
            }
            buffer = new_buf;
        }
    }
    buffer[total_read] = '\0';

    int status = esp_http_client_get_status_code(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    log_http_response("cache", url, status, buffer, total_read);

    if (status != 200 || total_read == 0) {
        free(buffer);
        return false;
    }

    cJSON *root = cJSON_Parse(buffer);
    free(buffer);
    if (!root) {
        return false;
    }

    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    cache->count = 0;
    if (cJSON_IsArray(data) && cJSON_GetArraySize(data) > 0) {
        int count = cJSON_GetArraySize(data);
        if (count > MED_CACHE_MAX) {
            count = MED_CACHE_MAX;
        }
        for (int i = 0; i < count; ++i) {
            cJSON *item = cJSON_GetArrayItem(data, i);
            const char *name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "medicineName"));
            const char *dose = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "dosage"));
            const char *time = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "scheduledTime"));
            const char *status_txt = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "status"));
            const char *dose_id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "doseId"));
            if (!dose_id) {
                dose_id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "id"));
            }
            int slot = cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(item, "slot"))
                ? cJSON_GetObjectItemCaseSensitive(item, "slot")->valueint
                : 0;

            med_cache_item_t *dst = &cache->items[i];
            snprintf(dst->name, sizeof(dst->name), "%s", name ? name : "");
            snprintf(dst->dose, sizeof(dst->dose), "%s", dose ? dose : "");
            format_time_12h(time, dst->time_str, sizeof(dst->time_str));
            snprintf(dst->status, sizeof(dst->status), "%s", status_txt ? status_txt : "");
            snprintf(dst->dose_id, sizeof(dst->dose_id), "%s", dose_id ? dose_id : "");
            dst->slot = slot;
            cache->count++;
        }
    }

    cache->valid = true;
    med_cache_set_updated(cache);
    med_cache_save_nvs(cache_key, cache);

    cJSON_Delete(root);
    return true;
}

static bool time_sync_from_api(void)
{
    char url[256];
    snprintf(url, sizeof(url), "%s%s", BACKEND_BASE_URL, TIME_API_PATH);
    ESP_LOGI(TAG, "HTTP GET %s", url);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 5000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Time API client init failed");
        return false;
    }

    esp_http_client_set_method(client, HTTP_METHOD_GET);
    esp_http_client_set_header(client, "User-Agent", "DoseRight-ESP32");
    {
        char auth_header[128];
        snprintf(auth_header, sizeof(auth_header), "Bearer %s", DEVICE_SECRET);
        esp_http_client_set_header(client, "Authorization", auth_header);
    }
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Time API open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int buffer_size = (content_length > 0) ? (content_length + 1) : 2048;
    char *buffer = (char *)malloc(buffer_size);
    if (!buffer) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    int total_read = 0;
    int read_len = 0;
    while ((read_len = esp_http_client_read(client, buffer + total_read, buffer_size - 1 - total_read)) > 0) {
        total_read += read_len;
        if (total_read >= buffer_size - 1) {
            buffer_size *= 2;
            char *new_buf = (char *)realloc(buffer, buffer_size);
            if (!new_buf) {
                free(buffer);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return false;
            }
            buffer = new_buf;
        }
    }
    buffer[total_read] = '\0';

    int status = esp_http_client_get_status_code(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    log_http_response("time", url, status, buffer, total_read);

    if (status != 200 || total_read == 0) {
        free(buffer);
        return false;
    }

    cJSON *root = cJSON_Parse(buffer);
    free(buffer);
    if (!root) {
        return false;
    }

    cJSON *local_time_24 = cJSON_GetObjectItemCaseSensitive(root, "localTime24");
    cJSON *local_time_12 = cJSON_GetObjectItemCaseSensitive(root, "localTime12");
    if (!cJSON_IsString(local_time_12) && !cJSON_IsString(local_time_24)) {
        cJSON_Delete(root);
        return false;
    }

    time_base_epoch_seconds = 0;
    time_base_offset_min = 0;
    time_base_ms = 0;
    time_base_hour24 = -1;
    time_base_minute = -1;

    if (cJSON_IsString(local_time_12)) {
        snprintf(time_display, sizeof(time_display), "%s", local_time_12->valuestring);
    } else if (cJSON_IsString(local_time_24)) {
        int hour = 0;
        int minute = 0;
        if (sscanf(local_time_24->valuestring, "%d:%d", &hour, &minute) == 2) {
            const char *ampm = (hour >= 12) ? "PM" : "AM";
            int hour12 = hour % 12;
            if (hour12 == 0) {
                hour12 = 12;
            }
            snprintf(time_display, sizeof(time_display), "%02d:%02d %s", hour12, minute, ampm);
        } else {
            snprintf(time_display, sizeof(time_display), "--:--");
        }
    } else {
        snprintf(time_display, sizeof(time_display), "--:--");
    }
    time_display_valid = strcmp(time_display, "--:--") != 0;
    if (time_display_valid) {
        time_cache_save_nvs(time_display);
    }

    if (cJSON_IsString(local_time_24)) {
        int hour24 = 0;
        int minute = 0;
        if (sscanf(local_time_24->valuestring, "%d:%d", &hour24, &minute) == 2) {
            time_base_hour24 = hour24 % 24;
            time_base_minute = minute % 60;
            time_base_ms = esp_timer_get_time() / 1000;
        }
    }
    cJSON_Delete(root);
    return true;
}

static void time_sync_task(void *arg)
{
    (void)arg;
    while (true) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (wifi_is_connected() && (time_sync_requested || (now_ms - last_time_sync_ms >= TIME_RESYNC_INTERVAL_MS))) {
            time_sync_requested = false;
            bool synced = false;
            for (int attempt = 0; attempt < 3 && !synced; ++attempt) {
                synced = time_sync_from_api();
                if (!synced) {
                    vTaskDelay(pdMS_TO_TICKS(1000));
                }
            }

            time_synced = synced;
            if (synced) {
                last_time_sync_ms = now_ms;
                lvgl_port_lock(0);
                if (clock_label) {
                    lv_label_set_text(clock_label, time_display);
                }
                lvgl_port_unlock();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

static void backend_fetch_task(void *arg)
{
    (void)arg;
    int64_t last_fetch_ms = 0;

    while (true) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        bool should_fetch = backend_fetch_requested || (now_ms - last_fetch_ms >= BACKEND_FETCH_INTERVAL_MS);

        if (should_fetch && wifi_is_connected()) {
            backend_fetch_requested = false;
            last_fetch_ms = now_ms;
            lvgl_port_lock(0);
            set_main_data_fetching();
            lvgl_port_unlock();
            if (!backend_fetch_upcoming()) {
                lvgl_port_lock(0);
                set_main_data_error(backend_last_error);
                lvgl_port_unlock();
            }
            backend_fetch_cache("/api/hardware/taken", &cache_taken, "med_taken");
            backend_fetch_cache("/api/hardware/missed", &cache_missed, "med_missed");
            profile_screen_preload();
            if (pending_info_fetch) {
                pending_info_fetch = false;
                med_cache_t *cache = get_cache_for_path(pending_info_path);
                const char *key = get_cache_key_for_path(pending_info_path);
                if (cache && key && backend_fetch_cache(pending_info_path, cache, key)) {
                    if (current_info_path[0] != '\0' && strcmp(current_info_path, pending_info_path) == 0) {
                        lvgl_port_lock(0);
                        render_med_cache(pending_info_title, cache, false);
                        lvgl_port_unlock();
                    }
                }
            }
        } else if (!wifi_is_connected()) {
            lvgl_port_lock(0);
            if (!apply_cached_upcoming_to_main()) {
                set_main_data_error("WiFi not connected");
            }
            lvgl_port_unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        wifi_start_scan();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        uint16_t ap_count = 0;
        esp_wifi_scan_get_ap_num(&ap_count);

        if (ap_count == 0) {
            lvgl_port_lock(0);
            wifi_list_screen_set_status_text("No networks found");
            wifi_list_screen_set_ap_records(NULL, 0);
            lvgl_port_unlock();
            return;
        }

        wifi_ap_record_t *ap_records = (wifi_ap_record_t *)calloc(ap_count, sizeof(wifi_ap_record_t));
        if (!ap_records) {
            return;
        }

        esp_wifi_scan_get_ap_records(&ap_count, ap_records);
        lvgl_port_lock(0);
        wifi_list_screen_set_status_text("Tap a network");
        wifi_list_screen_set_ap_records(ap_records, ap_count);
        lvgl_port_unlock();

        free(ap_records);
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        lvgl_port_lock(0);
        wifi_list_screen_set_status_text("Disconnected");
        set_wifi_status_state(false, NULL);
        lvgl_port_unlock();
        if (wifi_auto_connecting && wifi_auto_connect_next()) {
            return;
        }
        if (!wifi_is_connected()) {
            route_to_wifi_list();
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        wifi_auto_connecting = false;
        wifi_auto_index = -1;
        wifi_creds_add_or_update(selected_ssid, selected_password);
        lvgl_port_lock(0);
        wifi_list_screen_set_status_text("Connected");
        if (selected_ssid[0] != '\0') {
            char buf[64];
            lv_snprintf(buf, sizeof(buf), "WiFi: %s", selected_ssid);
            set_wifi_status_state(true, buf);
        } else {
            set_wifi_status_state(true, NULL);
        }
        lvgl_port_unlock();
        backend_fetch_requested = true;
        time_sync_requested = true;
        route_to_screen3();
    }
}

static void wifi_init_sta(void)
{
    if (wifi_ready) {
        return;
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(ret);
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    wifi_ready = true;
}

static void connect_selected_ssid(const char *password)
{
    wifi_connect_start(selected_ssid, password, false);
}

static void on_wifi_list_ssid_selected(const char *ssid)
{
    if (!ssid) {
        return;
    }

    strncpy(selected_ssid, ssid, sizeof(selected_ssid) - 1);
    selected_ssid[sizeof(selected_ssid) - 1] = '\0';

    if (ui_Label7) {
        char buf[64];
        lv_snprintf(buf, sizeof(buf), "SSID : %s", selected_ssid);
        lv_label_set_text(ui_Label7, buf);
    }

    if (ui_TextArea2) {
        lv_textarea_set_text(ui_TextArea2, "");
    }

    route_to_wifi();
}

static void on_wifi_list_back(void)
{
    route_to_screen3();
}

static void on_keyboard_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        const char *pwd = "";
        if (ui_TextArea2) {
            pwd = lv_textarea_get_text(ui_TextArea2);
        }
        snprintf(selected_password, sizeof(selected_password), "%s", pwd ? pwd : "");
        lvgl_port_lock(0);
        set_wifi_status_state(false, "WiFi: Connecting...");
        lvgl_port_unlock();
        connect_selected_ssid(pwd);
        route_to_screen3();
    } else if (code == LV_EVENT_CANCEL) {
        route_to_wifi_list();
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting DoseRight UI");

    /* 1. Start the display (CRITICAL) */
    bsp_display_start();

    /* 2. Turn on backlight */
    bsp_display_backlight_on();

    /* 3. Init LVGL port */
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&lvgl_cfg);
    init_main_button();

    /* 4. Create UI */
    lvgl_port_lock(0);
    ui_init();
    wifi_list_screen_init();
    wifi_list_screen_set_on_ssid_selected(on_wifi_list_ssid_selected);
    wifi_list_screen_set_on_back(on_wifi_list_back);
    ensure_menu_screen();
    main_menu_screen_set_on_back(on_main_menu_back);
    help_screen_init();
    help_screen_set_on_back(on_submenu_back);
    settings_screen_init();
    settings_screen_set_on_back(on_submenu_back);
    profile_screen_init();
    profile_screen_set_on_back(on_submenu_back);
    alert_screen_init();
    alert_screen_set_on_pick(on_alert_pick_action);
    alert_screen_set_on_skip(on_alert_skip_action);


    /* 5. Wire up interactions */
    if (ui_Button2) {
        lv_obj_add_event_cb(ui_Button2, on_button_to_screen2, LV_EVENT_CLICKED, NULL);
    }
    if (ui_Button3) {
        lv_obj_add_event_cb(ui_Button3, on_button_to_screen4, LV_EVENT_CLICKED, NULL);
    }
    if (ui_Button1) {
        lv_obj_add_event_cb(ui_Button1, on_button_to_wifi, LV_EVENT_CLICKED, NULL);
    }
    if (wifi_status_label) {
        lv_obj_add_event_cb(wifi_status_label, on_wifi_logo_clicked, LV_EVENT_CLICKED, NULL);
    }
    if (ui_Keyboard1) {
        lv_obj_add_event_cb(ui_Keyboard1, on_keyboard_event, LV_EVENT_ALL, NULL);
    }

    /* Clock on main menu */
    ensure_clock_label();
    ensure_profile_button();
    ensure_wifi_status_label();
    if (ui_Screen3) {
        lv_obj_add_event_cb(ui_Screen3, on_main_screen_gesture, LV_EVENT_GESTURE, NULL);
    }
    ensure_main_data_labels();
    motor_init();
    servo_init();
    servo_current_deg = 180;
    servo_set_degree(servo_current_deg);
    ir_sensor_set_enabled(false);
    ir_sensor_start();
    lv_timer_create(clock_timer_cb, 1000, NULL);

    /* Auto-advance from loading screen SET SCREEN TIME BOOT SCREEN*/
    if (ui_Bar1) {
        boot_progress_value = 0;
        lv_bar_set_value(ui_Bar1, 0, LV_ANIM_OFF);
        boot_progress_timer = lv_timer_create(boot_progress_timer_cb, BOOT_PROGRESS_INTERVAL_MS, NULL);
    }
    lv_timer_create(loading_timer_cb, 3000, NULL);
    lvgl_port_unlock();

    wifi_init_sta();
    wifi_creds_load();
    wifi_auto_connect_start();
    stepper_slot_load();
    med_cache_load_all();
    time_cache_load_nvs();
    lvgl_port_lock(0);
    apply_cached_upcoming_to_main();
    update_clock_text();
    lvgl_port_unlock();
    xTaskCreate(backend_fetch_task, "backend_fetch", 8192, NULL, 5, NULL);
    xTaskCreate(time_sync_task, "time_sync", 4096, NULL, 5, NULL);
    xTaskCreate(heartbeat_task, "heartbeat", 4096, NULL, 4, NULL);
}
