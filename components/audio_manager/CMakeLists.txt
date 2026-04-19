idf_component_register(
    SRCS "src/audio_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES
        buddy_hal
        log
        freertos
)
