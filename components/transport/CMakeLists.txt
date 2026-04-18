idf_component_register(
    SRCS
        "src/transport.c"
        "src/transport_ble.c"
        "src/transport_ws.c"
        "src/transport_usb.c"
    INCLUDE_DIRS "include"
    REQUIRES
        freertos
        log
        bt
        esp_event
        esp_driver_usb_serial_jtag
    PRIV_REQUIRES
        espressif__esp_websocket_client
)
