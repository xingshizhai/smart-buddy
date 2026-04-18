idf_component_register(
    SRCS "src/imu_monitor.c"
    INCLUDE_DIRS "include"
    REQUIRES buddy_hal agent_core freertos log esp_timer
)
