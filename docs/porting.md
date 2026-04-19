# Porting to a New Board

Smart Buddy separates hardware access behind a function-table HAL defined in `components/buddy_hal/`. Adding support for a new board means implementing those function tables — no changes to the transport, protocol, UI, or agent_core components are needed.

---

## How the HAL Works

Each subsystem has its own handle type defined in `components/buddy_hal/include/buddy_hal/`:

| Header | Handle type | Subsystem |
|--------|-------------|-----------|
| `hal_display.h` | `hal_display_t` | Display flush, backlight |
| `hal_touch.h` | `hal_touch_t` | Touch read, callback registration |
| `hal_audio.h` | `hal_audio_t` | PCM playback and capture |
| `hal_button.h` | `hal_buttons_t` | Button event callbacks |
| `hal_imu.h` | `hal_imu_t` | Accelerometer / gyroscope read |
| `hal_led.h` | `hal_led_t` | RGB LED control |
| `hal_storage.h` | _(static functions)_ | NVS key-value storage |

Each handle is a struct of function pointers. The board implementation fills in those pointers at init time and stores any board-specific state in a `priv` field.

A global `hal_handles_t g_hal` struct (declared in `hal.h`, defined in `hal.c`) holds all active handles. `main.c` populates it during startup by calling the board's `hal_*_create()` factory functions.

---

## Step-by-Step

### 1. Create the board directory

```
components/boards/<your_board>/
├── CMakeLists.txt
├── idf_component.yml    # optional: declare BSP or driver dependencies
├── include/
│   └── board_<your_board>.h
└── src/
    ├── board_display.c
    ├── board_touch.c
    ├── board_audio.c
    ├── board_button.c
    ├── board_imu.c
    └── board_led.c
```

### 2. Write `CMakeLists.txt`

```cmake
idf_component_register(
    SRCS
        "src/board_display.c"
        "src/board_touch.c"
        "src/board_audio.c"
        "src/board_button.c"
        "src/board_imu.c"
        "src/board_led.c"
    INCLUDE_DIRS "include"
    REQUIRES
        buddy_hal
        log
        # add your BSP or driver components here
)
```

### 3. Implement each HAL source file

Below are the function signatures each file must implement. Refer to `components/boards/esp32s3_box3/src/` for a complete working example.

#### board_display.c

```c
#include "buddy_hal/hal_display.h"

// flush_cb: called by LVGL to push a rectangle of pixels to the display
// backlight_set: 0 = off, 100 = full brightness
esp_err_t hal_display_create(const hal_display_cfg_t *cfg, hal_display_t **out);
void      hal_display_destroy(hal_display_t *disp);
```

The `hal_display_t` struct must include at minimum:
```c
struct hal_display_s {
    esp_err_t (*flush)(hal_display_t*, const lv_area_t*, lv_color_t*);
    esp_err_t (*backlight_set)(hal_display_t*, uint8_t percent);
    void *priv;
};
```

#### board_touch.c

```c
esp_err_t hal_touch_create(hal_touch_t **out);
void      hal_touch_destroy(hal_touch_t *touch);
```

`hal_touch_t` exposes `read()` returning x/y/pressed and `register_cb()` for interrupt-driven boards.

#### board_audio.c

```c
esp_err_t hal_audio_create(const hal_audio_cfg_t *cfg, hal_audio_t **out);
```

`hal_audio_t` exposes `play(pcm, len)`, `record_start()`, `record_read(buf, len)`, `record_stop()`, `set_volume(0-100)`.

#### board_button.c

```c
esp_err_t hal_buttons_create(hal_buttons_t **out);
void      hal_buttons_destroy(hal_buttons_t *btns);
```

`hal_buttons_t` exposes `register_cb(id, event, cb, ctx)` and `is_pressed(id)`. Button IDs (`HAL_BTN_0`, `HAL_BTN_1`, `HAL_BTN_2`) map to physical buttons in board order.

#### board_imu.c

```c
esp_err_t hal_imu_create(hal_imu_t **out);
void      hal_imu_destroy(hal_imu_t *imu);
```

`hal_imu_t` exposes `read_accel(x, y, z)` and `read_gyro(x, y, z)` in milli-g and milli-dps respectively.

#### board_led.c

```c
esp_err_t hal_led_create(hal_led_t **out);
void      hal_led_destroy(hal_led_t *led);
```

`hal_led_t` exposes `set_rgb(r, g, b)`, `blink(r, g, b, period_ms)`, and `off()`.

### 4. If your board lacks a subsystem

Return a stub handle with `NULL` function pointers for any subsystem your board does not have. Callers check for `NULL` before invoking:

```c
// Minimal stub for a board without an IMU
esp_err_t hal_imu_create(hal_imu_t **out) {
    *out = NULL;    // imu_monitor_start() checks for NULL and skips
    return ESP_OK;
}
```

### 5. Build with your board

Pass `SMART_BUDDY_BOARD` on the command line:

```bash
idf.py -DSMART_BUDDY_BOARD=<your_board> build
```

The top-level `CMakeLists.txt` adds `components/boards/<your_board>` to `EXTRA_COMPONENT_DIRS`, which is how IDF discovers your implementation.

To make your board the default, change the fallback in `CMakeLists.txt`:

```cmake
if(NOT DEFINED SMART_BUDDY_BOARD)
    set(SMART_BUDDY_BOARD "your_board")
endif()
```

---

## ESP32-S3-BOX-3 Reference Implementation

The reference implementation in `components/boards/esp32s3_box3/` uses:

| Subsystem | Driver |
|-----------|--------|
| Display + Touch | `espressif/esp-box-3` BSP (`bsp_display_new`, `bsp_touch_new`) |
| Audio | `espressif/esp_codec_dev` via BSP |
| Buttons | `espressif/button` v4 (`iot_button_create`, `iot_button_register_cb`) |
| IMU | `espressif/icm42670` v2 (`icm42670_create`, `icm42670_config`) |
| LED | GPIO via BSP pin definitions |
| I2C bus | `bsp_i2c_get_handle()` returning `i2c_master_bus_handle_t` |

Notable quirks documented in the source:
- `bsp_touch_new()` is in `bsp/touch.h`, not `bsp/esp-bsp.h` — include it separately.
- `icm42670_create` takes 3 arguments: `(i2c_master_bus_handle_t, addr, &handle_out)`.
- `iot_button_register_cb` takes 5 arguments in v4: `(handle, event, NULL, cb, arg)`.
- The HAL component is named `buddy_hal` (not `hal`) to avoid collision with IDF's internal pseudo-component.
