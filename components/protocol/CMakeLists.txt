idf_component_register(
    SRCS
        "src/protocol.c"
        "src/proto_openclaw.c"
        "src/proto_hermes.c"
    INCLUDE_DIRS "include"
    REQUIRES
        buddy_hal
        cjson
        log
        esp_timer
)
