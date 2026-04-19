# Smart Buddy

An ESP32-S3-BOX-3 firmware that turns your device into a physical companion for AI agents. It displays the agent's working state on the touchscreen, and lets you approve or deny tool-use requests with a tap — over BLE, USB, or WebSocket.

Inspired by [claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy). Supports three protocol modes selectable in `menuconfig`: **Claude Buddy** (drop-in compatible with claude-desktop-buddy), **OpenClaw**, and **Hermes**.

[中文文档](README_zh.md)

---

## Hardware

| Component | Spec |
|-----------|------|
| Board | ESP32-S3-BOX-3 |
| Display | 2.4" 320 × 240 ILI9341, capacitive touch (GT911) |
| Audio | ES7210 mic array + ES8311 speaker codec |
| IMU | ICM-42670-P (shake / face-down detection) |
| Buttons | 3 hardware buttons |
| Flash / RAM | 16 MB Flash + 8 MB PSRAM |

---

## Prerequisites

- **ESP-IDF v5.4 or v6.0** — see the [Espressif get-started guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/)
- Python 3.8+, CMake 3.24+, Ninja (all included in the IDF installer)
- USB-C cable connected to the BOX-3's JTAG/UART port

### Install ESP-IDF (first time only)

```bash
git clone --recursive https://github.com/espressif/esp-idf.git ~/esp-idf
cd ~/esp-idf
git checkout v6.0          # or v5.4
./install.sh esp32s3
```

---

## Quick Start

### 1. Clone

```bash
git clone https://github.com/yourorg/smart-buddy.git
cd smart-buddy
```

### 2. Activate ESP-IDF

```bash
source ~/esp-idf/export.sh
```

> Run this in every new terminal session. You can add it to your shell profile.

### 3. Configure

```bash
idf.py menuconfig
```

Key settings under **Smart-Buddy Configuration**:

| Setting | Path | Default |
|---------|------|---------|
| Protocol mode | Protocol Adapters → Buddy Protocol Mode | Claude Buddy |
| BLE device name | Transport Layer → BLE Advertised Name | SmartBuddy |
| WebSocket server URL | Transport Layer → Default WebSocket server URL | ws://192.168.1.100:8080/buddy |
| Wi-Fi SSID | Wi-Fi → Default Wi-Fi SSID | _(empty)_ |
| Wi-Fi password | Wi-Fi → Default Wi-Fi Password | _(empty)_ |

Save with `S`, exit with `Q`.

### 4. Build

```bash
idf.py build
```

The first build downloads managed components (~200 MB) and takes 3–5 minutes. Subsequent builds are fast and incremental.

### 5. Flash and monitor

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

Replace `/dev/ttyACM0` with your port:

| OS | Typical port |
|----|-------------|
| Linux | `/dev/ttyACM0` |
| macOS | `/dev/tty.usbmodem*` |
| Windows | `COM3` (check Device Manager) |

The device boots, shows the logo screen, then transitions to the main companion screen.

---

## Connecting an Agent

All three transports use the same **newline-delimited JSON** frame format. You can connect via all three simultaneously; the device broadcasts outgoing messages to every connected transport.

### USB (simplest)

The device appears as a USB serial port as soon as it boots — no Wi-Fi or pairing required.

```bash
# Send a heartbeat frame
echo '{"running":1,"waiting":0,"tokens":1234}' > /dev/ttyACM0

# Read responses (keep open)
cat /dev/ttyACM0
```

### BLE

The device advertises as **SmartBuddy** using the Nordic UART Service (NUS). Connect with any BLE terminal app (nRF Connect, LightBlue, etc.) and write JSON frames to the RX characteristic.

### WebSocket

Set the server URL in menuconfig and provide Wi-Fi credentials. The device connects automatically on boot and reconnects with exponential back-off on drops.

---

## Protocol Modes

Select in menuconfig → **Protocol Adapters → Buddy Protocol Mode**. The choice is compiled in; rebuilding after changing the mode is all that's needed.

### Claude Buddy _(default)_

Wire-compatible with [claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy).

```jsonc
// Agent → device: heartbeat
{"running": 2, "waiting": 1, "tokens": 8500}

// Agent → device: tool-use permission request
{"id": "abc123", "tool": "bash", "description": "Run shell command"}

// Device → agent: permission response
{"decision": "allow_once", "id": "abc123"}
// or: {"decision": "deny", "id": "abc123"}
```

### OpenClaw

```jsonc
// Agent → device: heartbeat
{"total": 3, "running": 2, "tokens": 8500}

// Agent → device: permission request
{"prompt": {"id": "abc123", "tool": "bash", "hint": "Run shell command"}}

// Device → agent: permission response
{"cmd": "permission", "id": "abc123", "decision": "once"}
```

### Hermes

Field names are configurable at compile time. See [docs/protocol.md](docs/protocol.md).

---

## On-Device Controls

| Input | Action |
|-------|--------|
| Left button — long press | Status screen (connections, tokens, free heap) |
| Right button — long press | Settings screen |
| Touchscreen — **Approve** | Grant the pending tool-use request |
| Touchscreen — **Deny** | Deny the pending tool-use request |

**Gestures (IMU):**

| Gesture | Effect |
|---------|--------|
| Shake | Dizzy animation (2 s), then returns |
| Face-down | Screen dims / sleeps |
| Face-up | Screen wakes |

---

## Device States

```
SLEEP  ──connect──►  IDLE  ──agent working──►  BUSY
                      ▲                           │
                      │                 tool-use request
                      │                           ▼
                   resolved ◄──────────────  ATTENTION
                                              (touch to approve/deny)
```

Additional transient states: **CELEBRATE** (token milestone), **DIZZY** (shake), **HEART** (fast approval).

---

## Project Layout

```
smart-buddy/
├── main/                   # app_main, Kconfig.projbuild
├── components/
│   ├── buddy_hal/          # HAL interface headers (board-independent)
│   ├── boards/
│   │   ├── esp32s3_box3/   # ESP32-S3-BOX-3 HAL implementation
│   │   └── esp32s3_devkit/ # Minimal stub for generic ESP32-S3
│   ├── transport/          # BLE / WebSocket / USB transports
│   ├── protocol/           # Claude Buddy / OpenClaw / Hermes adapters
│   ├── agent_core/         # Central event queue and state-machine driver
│   ├── state_machine/      # 7-state FSM
│   ├── ui/                 # LVGL screens (boot, main, approval, status, settings)
│   └── imu_monitor/        # 50 Hz gesture detection task
├── assets/                 # LVGL fonts and avatar images
├── docs/                   # In-depth technical documentation
└── tools/                  # Image converter, version header generator
```

---

## Further Reading

| Document | Contents |
|----------|----------|
| [docs/architecture.md](docs/architecture.md) | Component diagram, FreeRTOS task table, full state machine |
| [docs/protocol.md](docs/protocol.md) | Complete wire-format reference for all three modes |
| [docs/porting.md](docs/porting.md) | How to add support for a different board |

---

## Troubleshooting

**`idf.py: command not found`** — Run `source ~/esp-idf/export.sh` first.

**Port permission denied on Linux** — `sudo usermod -aG dialout $USER`, then log out and back in.

**BLE fails to initialize** — Known NimBLE / IDF 6.x compatibility issue on some chip revisions. USB and WebSocket transports are unaffected and fully functional.

**WebSocket reconnects in a loop** — Expected when Wi-Fi credentials are not configured. The reconnection loop is harmless; set SSID/password in menuconfig or the on-device Settings screen.

**Screen stays black after 30 s** — The backlight timer is working as designed. Touch the screen or press any button to wake.

---

## License

GPL-3.0
