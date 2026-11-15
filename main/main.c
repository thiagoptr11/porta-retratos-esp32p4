#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp_board_extra.h"
#include "lvgl.h"
#include "jpeg_decoder.h"

#define PHOTO_DIR BSP_SD_MOUNT_POINT "/FOTOS"
#define SCREEN_W 1024
#define SCREEN_H 600
#define SLIDE_TIME_MS 10000
#define MAX_PHOTOS 512

static const char *TAG = "porta_retratos";

typedef struct {
    char **items;
    size_t count;
} photo_list_t;

static lv_obj_t *image_obj;
static lv_obj_t *status_label;
static lv_image_dsc_t current_image_dsc;
static uint8_t *current_image_data;

static bool is_jpeg(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot && (!strcasecmp(dot, ".jpg") || !strcasecmp(dot, ".jpeg"));
}

static void free_photo_list(photo_list_t *list)
{
    if (!list || !list->items) return;
    for (size_t i = 0; i < list->count; i++) free(list->items[i]);
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

static esp_err_t scan_photos(photo_list_t *list)
{
    DIR *dir = opendir(PHOTO_DIR);
    if (!dir) {
        ESP_LOGE(TAG, "Nao foi possivel abrir %s", PHOTO_DIR);
        return ESP_FAIL;
    }

    list->items = calloc(MAX_PHOTOS, sizeof(char *));
    if (!list->items) {
        closedir(dir);
        return ESP_ERR_NO_MEM;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && list->count < MAX_PHOTOS) {
        if (!is_jpeg(entry->d_name)) continue;

        size_t len = strlen(PHOTO_DIR) + strlen(entry->d_name) + 2;
        char *path = malloc(len);
        if (!path) continue;
        snprintf(path, len, "%s/%s", PHOTO_DIR, entry->d_name);

        struct stat st;
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
            list->items[list->count++] = path;
        } else {
            free(path);
        }
    }
    closedir(dir);
    ESP_LOGI(TAG, "Encontradas %u fotos JPG", (unsigned)list->count);
    return ESP_OK;
}

static uint8_t *read_file_to_psram(const char *path, size_t *file_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size <= 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    uint8_t *data = heap_caps_malloc((size_t)size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!data) data = malloc((size_t)size);
    if (!data) {
        fclose(f);
        return NULL;
    }

    size_t got = fread(data, 1, (size_t)size, f);
    fclose(f);
    if (got != (size_t)size) {
        free(data);
        return NULL;
    }
    *file_size = got;
    return data;
}

static uint8_t *decode_and_fit_jpeg(const char *path, uint16_t *out_w, uint16_t *out_h)
{
    size_t jpeg_size = 0;
    uint8_t *jpeg_data = read_file_to_psram(path, &jpeg_size);
    if (!jpeg_data) {
        ESP_LOGW(TAG, "Falha lendo %s", path);
        return NULL;
    }

    esp_jpeg_image_cfg_t cfg = {
        .indata = jpeg_data,
        .indata_size = (uint32_t)jpeg_size,
        .outbuf = NULL,
        .outbuf_size = 0,
        .out_format = JPEG_IMAGE_FORMAT_RGB565,
        .out_scale = JPEG_IMAGE_SCALE_0,
        .flags = { .swap_color_bytes = 1 },
    };
    esp_jpeg_image_output_t info = {0};
    esp_err_t err = esp_jpeg_get_image_info(&cfg, &info);
    if (err != ESP_OK || info.width == 0 || info.height == 0 || info.output_len == 0) {
        ESP_LOGW(TAG, "JPEG invalido ou formato nao suportado: %s (%s)", path, esp_err_to_name(err));
        free(jpeg_data);
        return NULL;
    }

    uint8_t *decoded = heap_caps_malloc(info.output_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!decoded) decoded = malloc(info.output_len);
    if (!decoded) {
        free(jpeg_data);
        ESP_LOGE(TAG, "Sem memoria para decodificar %s (%u bytes)", path, (unsigned)info.output_len);
        return NULL;
    }

    cfg.outbuf = decoded;
    cfg.outbuf_size = info.output_len;
    err = esp_jpeg_decode(&cfg, &info);
    free(jpeg_data);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Erro decodificando %s: %s", path, esp_err_to_name(err));
        free(decoded);
        return NULL;
    }

    float scale_w = (float)SCREEN_W / (float)info.width;
    float scale_h = (float)SCREEN_H / (float)info.height;
    float scale = fminf(1.0f, fminf(scale_w, scale_h));
    uint16_t dst_w = (uint16_t)((float)info.width * scale + 0.5f);
    uint16_t dst_h = (uint16_t)((float)info.height * scale + 0.5f);
    if (dst_w == 0 || dst_h == 0) {
        free(decoded);
        return NULL;
    }

    size_t dst_size = (size_t)dst_w * dst_h * 2;
    uint8_t *fitted = heap_caps_malloc(dst_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!fitted) fitted = malloc(dst_size);
    if (!fitted) {
        free(decoded);
        return NULL;
    }

    const uint16_t *src = (const uint16_t *)decoded;
    uint16_t *dst = (uint16_t *)fitted;
    for (uint32_t y = 0; y < dst_h; y++) {
        uint32_t sy = (uint32_t)((uint64_t)y * info.height / dst_h);
        for (uint32_t x = 0; x < dst_w; x++) {
            uint32_t sx = (uint32_t)((uint64_t)x * info.width / dst_w);
            dst[y * dst_w + x] = src[sy * info.width + sx];
        }
    }
    free(decoded);
    *out_w = dst_w;
    *out_h = dst_h;
    return fitted;
}

static void show_status(const char *message)
{
    bsp_display_lock(0);
    lv_label_set_text(status_label, message);
    lv_obj_center(status_label);
    bsp_display_unlock();
}

static bool show_photo(const char *path)
{
    uint16_t width = 0, height = 0;
    uint8_t *new_data = decode_and_fit_jpeg(path, &width, &height);
    if (!new_data) return false;

    lv_image_dsc_t new_dsc = {0};
    new_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    new_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    new_dsc.header.w = width;
    new_dsc.header.h = height;
    new_dsc.header.stride = width * 2;
    new_dsc.data_size = (uint32_t)width * height * 2;
    new_dsc.data = new_data;

    bsp_display_lock(0);
    lv_image_set_src(image_obj, NULL);
    lv_refr_now(NULL);
    if (current_image_data) {
        free(current_image_data);
        current_image_data = NULL;
    }
    current_image_dsc = new_dsc;
    current_image_data = new_data;
    lv_obj_add_flag(status_label, LV_OBJ_FLAG_HIDDEN);
    lv_image_set_src(image_obj, &current_image_dsc);
    lv_obj_center(image_obj);
    lv_refr_now(NULL);
    bsp_display_unlock();

    ESP_LOGI(TAG, "Exibindo %s (%ux%u)", path, width, height);
    return true;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Iniciando porta-retratos JC1060P470C_I");
    lv_display_t *display = bsp_display_start();
    if (!display) {
        ESP_LOGE(TAG, "Falha ao inicializar display");
        return;
    }
    bsp_display_backlight_on();

    esp_err_t sd_err = bsp_sdcard_mount();
    if (sd_err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao montar microSD: %s", esp_err_to_name(sd_err));
        return;
    }

    bsp_display_lock(0);
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    image_obj = lv_image_create(screen);
    lv_obj_center(image_obj);

    status_label = lv_label_create(screen);
    lv_label_set_text(status_label, "Procurando fotos em /FOTOS...");
    lv_obj_set_style_text_color(status_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_20, 0);
    lv_obj_center(status_label);
    bsp_display_unlock();

    photo_list_t photos = {0};
    if (scan_photos(&photos) != ESP_OK || photos.count == 0) {
        show_status("Nenhuma foto JPG encontrada em /FOTOS");
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    size_t index = 0;
    while (1) {
        if (show_photo(photos.items[index])) {
            vTaskDelay(pdMS_TO_TICKS(SLIDE_TIME_MS));
        } else {
            vTaskDelay(pdMS_TO_TICKS(1500));
        }
        index = (index + 1) % photos.count;
    }
}
