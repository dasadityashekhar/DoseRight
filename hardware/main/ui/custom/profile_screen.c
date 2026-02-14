#include "profile_screen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_wifi.h"
#include "esp_err.h"
#include "nvs.h"
#include "cJSON.h"
#include "lvgl.h"

static lv_obj_t *profile_screen = NULL;
static lv_obj_t *profile_title = NULL;
static lv_obj_t *back_btn = NULL;
static void (*back_cb)(void) = NULL;
static char *profile_cache_json = NULL;

static lv_obj_t *profile_body = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *device_id_label = NULL;
static lv_obj_t *patient_name_label = NULL;
static lv_obj_t *illness_label = NULL;
static lv_obj_t *allergy_label = NULL;
static lv_obj_t *caretaker_name_label = NULL;
static lv_obj_t *caretaker_rel_label = NULL;

static void profile_show_status(const char *text);
static void profile_show_card(void);
static void profile_set_label_text(lv_obj_t *label, const char *prefix, const char *value);
static void profile_build_list_text(char *out, size_t out_len, const char *label, cJSON *array);
static lv_obj_t *profile_create_section_header(lv_obj_t *parent, const char *title);

static void profile_cache_load_nvs(void)
{
    nvs_handle_t handle;
    if (nvs_open("doseright", NVS_READONLY, &handle) != ESP_OK) {   
        return;
    }
    size_t len = 0;
    if (nvs_get_str(handle, "profile_json", NULL, &len) == ESP_OK && len > 1) {
        char *buf = (char *)malloc(len);
        if (buf && nvs_get_str(handle, "profile_json", buf, &len) == ESP_OK) {
            if (profile_cache_json) {
                free(profile_cache_json);
            }
            profile_cache_json = buf;
        } else if (buf) {
            free(buf);
        }
    }
    nvs_close(handle);
}

static void profile_cache_save_nvs(const char *json)
{
    if (!json) {
        return;
    }
    nvs_handle_t handle;
    if (nvs_open("doseright", NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    nvs_set_str(handle, "profile_json", json);
    nvs_commit(handle);
    nvs_close(handle);
}

// NOTE: Fill these locally before building; do not commit real values.
static const char *PROFILE_BASE_URL = "";
static const char *PROFILE_DEVICE_ID = "";
static const char *PROFILE_SECRET = "";

static void profile_render_json(const char *json)
{
    if (!profile_body) {
        return;
    }
    if (!json) {
        profile_show_status("No data");
        return;
    }

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        profile_show_status("Invalid response");
        return;
    }

    cJSON *device = cJSON_GetObjectItemCaseSensitive(root, "device");
    cJSON *patient = cJSON_GetObjectItemCaseSensitive(root, "patient");
    cJSON *support = cJSON_GetObjectItemCaseSensitive(root, "support");

    const char *device_id = NULL;
    if (cJSON_IsObject(device)) {
        device_id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(device, "id"));
    }
    if (!device_id || device_id[0] == '\0') {
        device_id = PROFILE_DEVICE_ID;
    }

    const char *patient_name = NULL;
    if (cJSON_IsObject(patient)) {
        patient_name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(patient, "displayName"));
    }
    if ((!patient_name || patient_name[0] == '\0') && cJSON_IsObject(device)) {
        patient_name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(device, "name"));
    }

    profile_set_label_text(device_id_label, "Device ID: ", device_id);
    profile_set_label_text(patient_name_label, "Name: ", patient_name);

    cJSON *medical = NULL;
    if (cJSON_IsObject(patient)) {
        medical = cJSON_GetObjectItemCaseSensitive(patient, "medicalProfile");
    }

    char illness_text[256];
    char allergy_text[256];
    if (cJSON_IsObject(medical)) {
        profile_build_list_text(illness_text, sizeof(illness_text), "Illnesses", cJSON_GetObjectItemCaseSensitive(medical, "illnesses"));
        profile_build_list_text(allergy_text, sizeof(allergy_text), "Allergies", cJSON_GetObjectItemCaseSensitive(medical, "allergies"));
    } else {
        snprintf(illness_text, sizeof(illness_text), "Illnesses: None");
        snprintf(allergy_text, sizeof(allergy_text), "Allergies: None");
    }
    lv_label_set_text(illness_label, illness_text);
    lv_label_set_text(allergy_label, allergy_text);

    cJSON *caretaker = NULL;
    if (cJSON_IsObject(support)) {
        caretaker = cJSON_GetObjectItemCaseSensitive(support, "caretaker");
    }
    const char *caretaker_name = NULL;
    const char *caretaker_rel = NULL;
    if (cJSON_IsObject(caretaker)) {
        caretaker_name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(caretaker, "name"));
        caretaker_rel = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(caretaker, "relationship"));
    }

    profile_set_label_text(caretaker_name_label, "Caretaker: ", caretaker_name);
    profile_set_label_text(caretaker_rel_label, "Relationship: ", caretaker_rel);

    profile_show_card();
    cJSON_Delete(root);
}

static bool profile_wifi_connected(void)
{
    wifi_ap_record_t ap_info;
    return esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK;
}

static lv_obj_t *profile_create_section_header(lv_obj_t *parent, const char *title)
{
    if (!parent || !title) {
        return NULL;
    }

    lv_obj_t *header = lv_label_create(parent);
    lv_label_set_text(header, title);
    lv_obj_set_style_text_font(header, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(header, lv_color_hex(0x667085), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(header, 8, LV_PART_MAIN | LV_STATE_DEFAULT);

    return header;
}

static void profile_show_status(const char *text)
{
    if (!status_label) {
        return;
    }

    lv_label_set_text(status_label, text ? text : "");
    lv_obj_clear_flag(status_label, LV_OBJ_FLAG_HIDDEN);


}

static void profile_show_card(void)
{
    if (!status_label) {
        return;
    }

    lv_obj_add_flag(status_label, LV_OBJ_FLAG_HIDDEN);
}

static void profile_set_label_text(lv_obj_t *label, const char *prefix, const char *value)
{
    if (!label) {
        return;
    }

    const char *safe_value = (value && value[0] != '\0') ? value : "--";
    char line[160];
    if (prefix && prefix[0] != '\0') {
        snprintf(line, sizeof(line), "%s%s", prefix, safe_value);
    } else {
        snprintf(line, sizeof(line), "%s", safe_value);
    }
    lv_label_set_text(label, line);
}

static void profile_build_list_text(char *out, size_t out_len, const char *label, cJSON *array)
{
    if (!out || out_len == 0) {
        return;
    }

    if (!label) {
        label = "";
    }

    if (!cJSON_IsArray(array) || cJSON_GetArraySize(array) == 0) {
        snprintf(out, out_len, "%s: None", label);
        return;
    }

    size_t used = snprintf(out, out_len, "%s:\n", label);
    int count = cJSON_GetArraySize(array);
    for (int i = 0; i < count && used < out_len; ++i) {
        const char *value = cJSON_GetStringValue(cJSON_GetArrayItem(array, i));
        if (!value) {
            continue;
        }
        int wrote = snprintf(out + used, out_len - used, "- %s%s", value, (i == count - 1) ? "" : "\n");
        if (wrote < 0) {
            break;
        }
        used += (size_t)wrote;
    }
}

static void on_back_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (back_cb) {
        back_cb();
    }
}

static void profile_fetch_and_render(void)
{
    if (!profile_body) {
        return;
    }

    if (profile_cache_json) {
        profile_render_json(profile_cache_json);
        return;
    }

    profile_show_status("Loading...");

    if (!profile_wifi_connected()) {
        profile_show_status("WiFi not connected\nConnect to WiFi and try again");
        return;
    }

    char url[256];
    snprintf(url, sizeof(url), "%s/api/device/%s/profile", PROFILE_BASE_URL, PROFILE_DEVICE_ID);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 5000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        profile_show_status("Request failed");
        return;
    }

    char auth_header[128];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", PROFILE_SECRET);
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    esp_http_client_set_header(client, "Authorization", auth_header);

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        {
            char line[96];
            snprintf(line, sizeof(line), "Server unreachable\n%s", esp_err_to_name(err));
            profile_show_status(line);
        }
        return;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int buffer_size = (content_length > 0) ? (content_length + 1) : 4096;
    char *buffer = (char *)malloc(buffer_size);
    if (!buffer) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        profile_show_status("Out of memory");
        return;
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
                profile_show_status("Out of memory");
                return;
            }
            buffer = new_buf;
        }
    }
    buffer[total_read] = '\0';

    int status = esp_http_client_get_status_code(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (status != 200 || total_read == 0) {
        free(buffer);
        if (status > 0) {
            char line[64];
            snprintf(line, sizeof(line), "HTTP %d", status);
            {
                char msg[96];
                snprintf(msg, sizeof(msg), "Request failed\n%s", line);
                profile_show_status(msg);
            }
        } else {
            profile_show_status("No data received");
        }
        return;
    }

    cJSON *root = cJSON_Parse(buffer);
    free(buffer);
    if (!root) {
        profile_show_status("Invalid response");
        return;
    }

    {
        char *printed = cJSON_PrintUnformatted(root);
        if (printed) {
            if (profile_cache_json) {
                free(profile_cache_json);
            }
            profile_cache_json = printed;
            profile_cache_save_nvs(profile_cache_json);
        }
    }

    cJSON_Delete(root);
    profile_render_json(profile_cache_json);
}

void profile_screen_set_on_back(void (*cb)(void))
{
    back_cb = cb;
}

void profile_screen_init(void)
{
    if (profile_screen) {
        return;
    }

    profile_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(profile_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(profile_screen, lv_color_hex(0xF5F7FA), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(profile_screen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    profile_title = lv_label_create(profile_screen);
    lv_label_set_text(profile_title, "Profile");
    lv_obj_align(profile_title, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_text_font(profile_title, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(profile_title, lv_color_hex(0x1D2939), LV_PART_MAIN | LV_STATE_DEFAULT);

    back_btn = lv_btn_create(profile_screen);
    lv_obj_set_size(back_btn, 34, 28);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 6, 6);
    lv_obj_add_event_cb(back_btn, on_back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
    lv_obj_center(back_lbl);

    profile_body = lv_obj_create(profile_screen);
    lv_obj_set_size(profile_body, 300, 180);
    lv_obj_align(profile_body, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_scrollbar_mode(profile_body, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(profile_body, lv_color_hex(0xF5F7FA), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(profile_body, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(profile_body, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(profile_body, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(profile_body, LV_FLEX_FLOW_COLUMN);

    status_label = lv_label_create(profile_body);
    lv_obj_set_width(status_label, 280);
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x667085), LV_PART_MAIN | LV_STATE_DEFAULT);

    device_id_label = lv_label_create(profile_body);
    lv_obj_set_style_text_font(device_id_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(device_id_label, lv_color_hex(0x1D2939), LV_PART_MAIN | LV_STATE_DEFAULT);

    patient_name_label = lv_label_create(profile_body);
    lv_obj_set_style_text_font(patient_name_label, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(patient_name_label, lv_color_hex(0x344054), LV_PART_MAIN | LV_STATE_DEFAULT);

    illness_label = lv_label_create(profile_body);
    lv_obj_set_width(illness_label, 280);
    lv_label_set_long_mode(illness_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(illness_label, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(illness_label, lv_color_hex(0x1D2939), LV_PART_MAIN | LV_STATE_DEFAULT);

    allergy_label = lv_label_create(profile_body);
    lv_obj_set_width(allergy_label, 280);
    lv_label_set_long_mode(allergy_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(allergy_label, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(allergy_label, lv_color_hex(0xB42318), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(allergy_label, lv_color_hex(0xFEE4E2), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(allergy_label, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(allergy_label, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(allergy_label, 6, LV_PART_MAIN | LV_STATE_DEFAULT);

    caretaker_name_label = lv_label_create(profile_body);
    lv_obj_set_style_text_font(caretaker_name_label, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(caretaker_name_label, lv_color_hex(0x1D2939), LV_PART_MAIN | LV_STATE_DEFAULT);

    caretaker_rel_label = lv_label_create(profile_body);
    lv_obj_set_style_text_font(caretaker_rel_label, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(caretaker_rel_label, lv_color_hex(0x344054), LV_PART_MAIN | LV_STATE_DEFAULT);

    profile_show_status("Loading...");

    profile_cache_load_nvs();
}

void profile_screen_show(void)
{
    if (!profile_screen) {
        profile_screen_init();
    }
    if (!profile_cache_json) {
        profile_cache_load_nvs();
    }
    profile_fetch_and_render();
    lv_scr_load_anim(profile_screen, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
}

void profile_screen_preload(void)
{
    if (!profile_wifi_connected()) {
        return;
    }

    char url[256];
    snprintf(url, sizeof(url), "%s/api/device/%s/profile", PROFILE_BASE_URL, PROFILE_DEVICE_ID);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 5000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return;
    }

    char auth_header[128];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", PROFILE_SECRET);
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    esp_http_client_set_header(client, "Authorization", auth_header);

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int buffer_size = (content_length > 0) ? (content_length + 1) : 4096;
    char *buffer = (char *)malloc(buffer_size);
    if (!buffer) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return;
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
                return;
            }
            buffer = new_buf;
        }
    }
    buffer[total_read] = '\0';

    int status = esp_http_client_get_status_code(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (status != 200 || total_read == 0) {
        free(buffer);
        return;
    }

    cJSON *root = cJSON_Parse(buffer);
    free(buffer);
    if (!root) {
        return;
    }

    char *printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return;
    }

    if (profile_cache_json) {
        free(profile_cache_json);
    }
    profile_cache_json = printed;
    profile_cache_save_nvs(profile_cache_json);
}
