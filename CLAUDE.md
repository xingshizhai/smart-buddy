# ESP-IDF Build Environment

## Quick Start — Build / Flash / Monitor

**Always** use `build_esp32.ps1` to activate the ESP-IDF environment before running `idf.py`. Running `idf.py` directly will fail because the environment is not set up.

### Recommended command patterns

| Task | Command |
|------|---------|
| Full build + flash + monitor (one-shot) | `powershell -NoProfile -Command ". '$PWD\build_esp32.ps1' -ActivateOnly; idf.py -p COM3 flash monitor"` |
| Build only | `powershell -NoProfile -Command ". '$PWD\build_esp32.ps1'"` |
| Activate then manual | `powershell -NoProfile -Command ". '$PWD\build_esp32.ps1' -ActivateOnly; idf.py menuconfig"` |
| Flash only (after build) | `powershell -NoProfile -Command ". '$PWD\build_esp32.ps1' -ActivateOnly; idf.py -p COM3 flash"` |
| Monitor only | `powershell -NoProfile -Command ". '$PWD\build_esp32.ps1' -ActivateOnly; idf.py -p COM3 monitor"` |

### Key rules

1. **Must dot-source** (`.`) `build_esp32.ps1` so that environment variables (PATH, IDF_PATH etc.) are preserved in the current shell. Without dot-sourcing, the environment disappears when the script exits.
2. **Activate and `idf.py` must be in the same command** (same process). e.g. `". 'build_esp32.ps1' -ActivateOnly; idf.py build"`.
3. The `-ActivateOnly` flag uses `return` (not `exit`) so that subsequent commands in the same `-Command` line continue to execute.
4. The port is **COM3** on this machine.
5. If a serial monitor is already running and the port is busy, kill the existing monitor process first.

### Build script details

- `build_esp32.ps1` — Main PowerShell script. Loads config, activates ESP-IDF (prefer EIM over `export.ps1`), then runs `idf.py build` by default.
- `build_esp32.bat` — CMD wrapper that loads `idf-env.bat` then delegates to the `.ps1` script.
- `idf-env.ps1` / `idf-env.bat` — Local config with `IDF_PATH`, `IDF_TOOLS_PATH`, `IDF_PYTHON_ENV_PATH`.

### Project info

- **Target**: ESP32-S3
- **ESP-IDF version**: v6.0.1 (NimBLE BLE stack)
- **BLE Device name**: Auto-generated as `BuddyXXYY` from MAC address (bytes 4-5)
- **BLE Advertising**: NUS 128-bit UUID in adv packet, device name in scan response
- **Serial port**: COM3
- **Transport layer**: BLE (NUS), Wi-Fi WebSocket, USB CDC-ACM

### Network

Web access (WebFetch, WebSearch) requires proxy `127.0.0.1:10808`.
