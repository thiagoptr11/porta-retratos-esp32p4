#include <stdio.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp_board_extra.h"
#include "lvgl.h"

static const char *TAG = "porta_retratos";

void app_main(void)
{
    ESP_LOGI(TAG, "Iniciando porta-retratos JC1060P470C_I");
    lv_display_t *display = bsp_display_start();
    if (display == NULL) {
        ESP_LOGE(TAG, "Falha ao inicializar o display");
        return;
    }
    bsp_display_backlight_on();

    esp_err_t sd_err = bsp_sdcard_mount();
    struct stat st;
    bool fotos_dir = (sd_err == ESP_OK &&
                      stat(BSP_SD_MOUNT_POINT "/FOTOS", &st) == 0 &&
                      (st.st_mode & S_IFDIR));

    bsp_display_lock(0);
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "PORTA-RETRATOS");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_26, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -55);

    lv_obj_t *status = lv_label_create(screen);
    if (sd_err != ESP_OK) {
        lv_label_set_text(status, "Display OK | Erro ao montar o cartao microSD");
    } else if (fotos_dir) {
        lv_label_set_text(status, "Display OK | Cartao OK | Pasta /FOTOS encontrada");
    } else {
        lv_label_set_text(status, "Display OK | Cartao OK | Crie a pasta /FOTOS");
    }
    lv_obj_set_style_text_color(status, lv_color_hex(0xB8C7D9), 0);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_16, 0);
    lv_obj_align(status, LV_ALIGN_CENTER, 0, 10);

    lv_obj_t *detail = lv_label_create(screen);
    lv_label_set_text(detail, "Teste inicial de hardware - ESP32-P4");
    lv_obj_set_style_text_color(detail, lv_color_hex(0x8293A7), 0);
    lv_obj_align(detail, LV_ALIGN_CENTER, 0, 55);
    bsp_display_unlock();

    ESP_LOGI(TAG, "Display iniciado; montagem do SD: %s", esp_err_to_name(sd_err));
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
