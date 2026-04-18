idf_component_register(
    SRCS "src/state_machine.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer freertos
)
