# Chinese Font Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enable LVGL 9.5's Source Han Sans SC 14 CJK font as a fallback behind Montserrat 14, so that dynamic Chinese text received over BLE renders correctly in the main message area, transcript area, and approval hint.

**Architecture:** A new `ui_fonts` module creates a mutable copy of `lv_font_montserrat_14` with `.fallback = &lv_font_source_han_sans_sc_14_cjk`. Three labels in `ui_manager.c` switch to this combined font. `ui_fonts_init()` is called in `main.c` before `ui_manager_init()`.

**Tech Stack:** ESP-IDF v6.0.1, LVGL 9.5.0, C99. Build: `source ~/.espressif/release-v6.0/esp-idf/export.sh && idf.py build`

---

## File Map

| Action | File | Purpose |
|--------|------|---------|
| Create | `components/ui/include/ui/ui_fonts.h` | Declares `g_font_14_cjk` and `ui_fonts_init()` |
| Create | `components/ui/src/ui_fonts.c` | Implements font copy + fallback wiring |
| Modify | `components/ui/CMakeLists.txt` | Adds `ui_fonts.c` to SRCS |
| Modify | `sdkconfig.defaults` | Enables `CONFIG_LV_FONT_SOURCE_HAN_SANS_SC_14_CJK` |
| Modify | `components/ui/src/ui_manager.c` | Switches 3 labels to `g_font_14_cjk` |
| Modify | `main/main.c` | Adds `ui_fonts_init()` call before `ui_manager_init()` |

---

## Task 1: Enable CJK font in sdkconfig.defaults

**Files:**
- Modify: `sdkconfig.defaults`

- [ ] **Step 1: Add font config line**

Open `sdkconfig.defaults`. After the existing font lines (currently ends with `CONFIG_LV_FONT_UNSCII_8=y`), append:

```
CONFIG_LV_FONT_SOURCE_HAN_SANS_SC_14_CJK=y
```

The complete relevant section should look like:
```
CONFIG_LV_FONT_MONTSERRAT_16=y
CONFIG_LV_FONT_MONTSERRAT_24=y
CONFIG_LV_FONT_UNSCII_8=y
CONFIG_LV_FONT_SOURCE_HAN_SANS_SC_14_CJK=y
```

- [ ] **Step 2: Verify the build still succeeds**

```bash
source ~/.espressif/release-v6.0/esp-idf/export.sh && idf.py build 2>&1 | tail -5
```

Expected output ends with:
```
Project build complete.
```

If build fails, check that `sdkconfig` was not manually edited — `sdkconfig.defaults` is the authoritative source; delete `sdkconfig` and rebuild if needed.

- [ ] **Step 3: Commit**

```bash
git add sdkconfig.defaults
git commit -m "feat: enable Source Han Sans SC 14 CJK font"
```

---

## Task 2: Create the ui_fonts module

**Files:**
- Create: `components/ui/include/ui/ui_fonts.h`
- Create: `components/ui/src/ui_fonts.c`
- Modify: `components/ui/CMakeLists.txt`

- [ ] **Step 1: Create the header**

Create `components/ui/include/ui/ui_fonts.h` with this exact content:

```c
#pragma once
#include "lvgl.h"

/* Montserrat 14 with Source Han Sans SC 14 CJK as fallback.
 * Call ui_fonts_init() once before ui_manager_init(). */
extern lv_font_t g_font_14_cjk;

void ui_fonts_init(void);
```

- [ ] **Step 2: Create the implementation**

Create `components/ui/src/ui_fonts.c` with this exact content:

```c
#include <string.h>
#include "lvgl.h"
#include "ui/ui_fonts.h"

lv_font_t g_font_14_cjk;

void ui_fonts_init(void)
{
    memcpy(&g_font_14_cjk, &lv_font_montserrat_14, sizeof(lv_font_t));
    g_font_14_cjk.fallback = &lv_font_source_han_sans_sc_14_cjk;
}
```

`lv_font_montserrat_14` is `const`; we copy it into a mutable global so we can set the `fallback` field. LVGL resolves missing glyphs through `.fallback` at render time — no runtime overhead.

- [ ] **Step 3: Add ui_fonts.c to the build**

Open `components/ui/CMakeLists.txt`. Add `"src/ui_fonts.c"` to the `SRCS` list:

```cmake
idf_component_register(
    SRCS
        "src/ui_fonts.c"
        "src/ui_theme.c"
        "src/ui_manager.c"
        "src/ui_screen_debug.c"
        "src/ui_screen_ble_debug.c"
        "src/ui_screen_stats.c"
        "src/ui_screen_info.c"
        "src/ui_screen_menu.c"
        "src/ui_screen_clock.c"
        "src/ui_button_router.c"
        "src/persona_data.c"
        "src/persona_driver.c"
    INCLUDE_DIRS "include"
    REQUIRES
        buddy_hal
        state_machine
        agent_core
        transport
        audio_manager
        log
    PRIV_REQUIRES
        lvgl__lvgl
        espressif__esp_lvgl_port
)
```

- [ ] **Step 4: Build to verify the new module compiles**

```bash
source ~/.espressif/release-v6.0/esp-idf/export.sh && idf.py build 2>&1 | tail -5
```

Expected: `Project build complete.`

If you see `undefined reference to lv_font_source_han_sans_sc_14_cjk`, `sdkconfig.defaults` was not picked up — delete `sdkconfig` and rebuild.

- [ ] **Step 5: Commit**

```bash
git add components/ui/include/ui/ui_fonts.h components/ui/src/ui_fonts.c components/ui/CMakeLists.txt
git commit -m "feat: add ui_fonts module with Montserrat+CJK fallback font"
```

---

## Task 3: Switch three labels to the CJK-capable font

**Files:**
- Modify: `components/ui/src/ui_manager.c`

There are three labels that receive dynamic content from BLE and must render Chinese. Each change is one line (two for `s_approval_hint` which gains an explicit font call).

- [ ] **Step 1: Update s_main_content_label (line ~269)**

Find this block in `screen_main_create()`:

```c
    s_main_content_label = lv_label_create(hero);
    lv_label_set_text(s_main_content_label, "Connect via Hardware Buddy");
    lv_obj_set_style_text_color(s_main_content_label, p->text_muted, 0);
    lv_obj_set_style_text_font(s_main_content_label, &lv_font_montserrat_14, 0);
```

Change `&lv_font_montserrat_14` to `&g_font_14_cjk`:

```c
    s_main_content_label = lv_label_create(hero);
    lv_label_set_text(s_main_content_label, "Connect via Hardware Buddy");
    lv_obj_set_style_text_color(s_main_content_label, p->text_muted, 0);
    lv_obj_set_style_text_font(s_main_content_label, &g_font_14_cjk, 0);
```

- [ ] **Step 2: Update s_transcript_label (line ~296)**

Find this block in `screen_main_create()`:

```c
    s_transcript_label = lv_label_create(s_transcript_area);
    lv_label_set_text(s_transcript_label, "");
    lv_obj_set_style_text_color(s_transcript_label, p->text_muted, 0);
    lv_obj_set_style_text_font(s_transcript_label, &lv_font_unscii_8, 0);
```

Change `&lv_font_unscii_8` to `&g_font_14_cjk`:

```c
    s_transcript_label = lv_label_create(s_transcript_area);
    lv_label_set_text(s_transcript_label, "");
    lv_obj_set_style_text_color(s_transcript_label, p->text_muted, 0);
    lv_obj_set_style_text_font(s_transcript_label, &g_font_14_cjk, 0);
```

Note: font size increases from 8 px to 14 px. The transcript container is 58 px tall so ~3 lines are visible at once; `LV_LABEL_LONG_SCROLL_CIRCULAR` handles the rest by scrolling.

- [ ] **Step 3: Update s_approval_hint (line ~326)**

Find this block in `screen_approval_create()`:

```c
    s_approval_hint = lv_label_create(panel);
    lv_label_set_text(s_approval_hint, "");
    lv_obj_set_width(s_approval_hint, 284);
    lv_label_set_long_mode(s_approval_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_approval_hint, p->text_muted, 0);
    lv_obj_align(s_approval_hint, LV_ALIGN_TOP_LEFT, 10, 58);
```

Add an explicit font assignment after the color line:

```c
    s_approval_hint = lv_label_create(panel);
    lv_label_set_text(s_approval_hint, "");
    lv_obj_set_width(s_approval_hint, 284);
    lv_label_set_long_mode(s_approval_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_approval_hint, p->text_muted, 0);
    lv_obj_set_style_text_font(s_approval_hint, &g_font_14_cjk, 0);
    lv_obj_align(s_approval_hint, LV_ALIGN_TOP_LEFT, 10, 58);
```

- [ ] **Step 4: Add the include at the top of ui_manager.c**

Find the existing includes block at the top of `ui_manager.c`:

```c
#include "ui/ui_theme.h"
```

Add after it:

```c
#include "ui/ui_theme.h"
#include "ui/ui_fonts.h"
```

- [ ] **Step 5: Build to verify no compile errors**

```bash
source ~/.espressif/release-v6.0/esp-idf/export.sh && idf.py build 2>&1 | tail -5
```

Expected: `Project build complete.`

- [ ] **Step 6: Commit**

```bash
git add components/ui/src/ui_manager.c
git commit -m "feat: switch dynamic content labels to CJK-capable font"
```

---

## Task 4: Wire ui_fonts_init() in main.c and final build

**Files:**
- Modify: `main/main.c`

- [ ] **Step 1: Add the include**

Find the UI include in `main/main.c`:

```c
#include "ui/ui_manager.h"
```

Add after it:

```c
#include "ui/ui_manager.h"
#include "ui/ui_fonts.h"
```

- [ ] **Step 2: Add the init call**

Find this block in `app_main()` (around line 173):

```c
    /* 4. UI — show boot screen */
    ESP_ERROR_CHECK(ui_manager_init());
```

Add `ui_fonts_init()` immediately before `ui_manager_init()`:

```c
    /* 4. UI — show boot screen */
    ui_fonts_init();
    ESP_ERROR_CHECK(ui_manager_init());
```

`ui_fonts_init()` has no failure path (it's a memcpy + pointer assignment), so no `ESP_ERROR_CHECK` wrapper is needed.

- [ ] **Step 3: Final build — verify binary size**

```bash
source ~/.espressif/release-v6.0/esp-idf/export.sh && idf.py build 2>&1 | grep -E "binary size|Project build"
```

Expected output:
```
smart_buddy.bin binary size 0x1... bytes. Smallest app partition is 0xf00000 bytes. ...% free.
Project build complete.
```

The binary should be ~120 KB larger than before (the CJK font bitmap data). Confirm it still fits well within the 15 MB partition.

- [ ] **Step 4: Commit**

```bash
git add main/main.c
git commit -m "feat: call ui_fonts_init before ui_manager_init"
```
