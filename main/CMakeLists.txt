idf_component_register(
    SRCS "main.c" "app_notify.c"
    INCLUDE_DIRS "."
    REQUIRES
        buddy_hal
        state_machine
        transport
        protocol
        agent_core
        imu_monitor
        audio_manager
        ui
        wifi_manager
        debug_screenshot
        nvs_flash
        esp_event
        esp_netif
        esp_timer
        log
        freertos
)
