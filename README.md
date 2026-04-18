# smart-buddy

AI Smart Buddy — ESP32-S3-BOX-3 firmware built with ESP-IDF v5.2+

A hardware companion for AI Agent frameworks (OpenClaw, Hermes), inspired by [claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy).

## Hardware: ESP32-S3-BOX-3

- 2.4" 320×240 capacitive touchscreen (LVGL UI)
- Dual digital microphones + speaker
- 3 hardware buttons + IMU (accelerometer + gyroscope)
- Wi-Fi + Bluetooth 5 LE, 16 MB Flash + 16 MB PSRAM

## Features

- **7-state companion**: Sleep / Idle / Busy / Attention / Celebrate / Dizzy / Heart
- **Touch approval**: approve or deny agent tool calls directly on the touchscreen
- **Triple transport**: BLE (Nordic UART Service), Wi-Fi/WebSocket, USB CDC-ACM
- **Dual protocol**: OpenClaw and Hermes adapters, runtime-switchable via Settings screen
- **Extensible HAL**: port to other ESP32-S3 boards by implementing 6 HAL function tables

## Quick Start

```bash
# Requires ESP-IDF v5.2+
idf.py build                                          # ESP32-S3-BOX-3 (default)
idf.py -DSMART_BUDDY_BOARD=esp32s3_devkit build       # Generic ESP32-S3
idf.py -p /dev/ttyUSB0 flash monitor
```

## Wire Protocol

Newline-delimited JSON over BLE NUS / WebSocket / USB CDC.

**Inbound heartbeat:**
```json
{"total":1,"running":1,"tokens":12450,"prompt":{"id":"abc","tool":"bash","hint":"rm -rf /tmp"}}
```

**Outbound approval:**
```json
{"cmd":"permission","id":"abc","decision":"once"}
```

## Porting

Create `components/boards/<board_name>/`, implement the 6 HAL interfaces (display, touch, audio, buttons, IMU, LED), build with `-DSMART_BUDDY_BOARD=<board_name>`.

## License

GPL-3.0
