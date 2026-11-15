idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_jpeg bsp_extra esp32_p4_function_ev_board lvgl__lvgl
)
