idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        buddy_hal
        state_machine
        transport
        protocol
        agent_core
        imu_monitor
        ui
        nvs_flash
        esp_event
        esp_netif
        log
        freertos
)
