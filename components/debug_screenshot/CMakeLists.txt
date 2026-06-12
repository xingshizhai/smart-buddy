idf_component_register(
    SRCS "src/debug_screenshot.c"
    INCLUDE_DIRS "include"
    REQUIRES
        lvgl__lvgl
        espressif__esp_lvgl_port
        freertos
        log
        lwip
        esp_psram
)
