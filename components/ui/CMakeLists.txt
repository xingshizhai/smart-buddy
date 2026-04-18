idf_component_register(
    SRCS "src/ui_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES
        buddy_hal
        state_machine
        agent_core
        transport
        log
    PRIV_REQUIRES
        lvgl__lvgl
        espressif__esp_lvgl_port
)
