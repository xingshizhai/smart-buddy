idf_component_register(
    SRCS
        "src/ui_fonts.c"
        "src/lv_font_noto_sans_sc_14.c"
        "src/ui_theme.c"
        "src/ui_manager.c"
        "src/ui_screen_debug.c"
        "src/ui_screen_ble_debug.c"
        "src/ui_screen_stats.c"
        "src/ui_screen_info.c"
        "src/ui_screen_menu.c"
        "src/ui_screen_clock.c"
        "src/ui_button_router.c"
        "src/persona_data.c"
        "src/persona_driver.c"
    INCLUDE_DIRS "include"
    REQUIRES
        buddy_hal
        state_machine
        agent_core
        transport
        audio_manager
        log
        lvgl__lvgl  # Public headers expose lv_font_t, lv_color_t, lv_obj_t
    PRIV_REQUIRES
        espressif__esp_lvgl_port
)
