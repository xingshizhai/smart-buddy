idf_component_register(
    SRCS
        "src/agent_core.c"
        "src/agent_stats.c"
    INCLUDE_DIRS "include"
    REQUIRES
        buddy_hal
        state_machine
        transport
        protocol
        ui
        freertos
        log
        esp_timer
        lwip
)
