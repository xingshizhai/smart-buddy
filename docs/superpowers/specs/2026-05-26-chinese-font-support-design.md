# Chinese Font Support Design

**Date:** 2026-05-26
**Status:** Approved

## Problem

The ESP32-S3 Smart Buddy UI uses LVGL 9.5 with Montserrat and UNSCII fonts. Neither supports CJK characters. Dynamic content received over BLE (Claude session messages, transcript entries, approval hints) may contain Chinese text, which currently renders as blank glyphs.

## Affected UI Locations

| Label | Data source | Current font | Issue |
|-------|-------------|--------------|-------|
| `s_main_content_label` | `session.msg` (48 bytes) | `lv_font_montserrat_14` | No CJK glyphs |
| `s_transcript_label` | `session.entries` (92 bytes × 8 lines) | `lv_font_unscii_8` | 8 px monospace, completely incapable of CJK |
| `s_approval_hint` | `approval_req.hint` (256 bytes) | `lv_font_montserrat_14` | No CJK glyphs |

Static UI labels (menu items, title bars, button text) are hardcoded ASCII — no changes needed.

## Approach: LVGL 9 Font Fallback Chain

LVGL 9 supports `lv_font_t.fallback`: when a glyph is missing in the primary font, LVGL resolves it recursively through the fallback chain.

Built-in `lv_font_montserrat_14` is declared `const`, so it cannot be modified directly. The solution is a mutable copy initialized at startup with the CJK font wired as its fallback:

- Latin characters render with Montserrat 14 (unchanged appearance)
- Chinese characters fall through to Source Han Sans SC 14 CJK
- No runtime overhead — fallback resolution happens per-glyph during render

## Font Resource

`lv_font_source_han_sans_sc_14_cjk` is already present in the LVGL 9.5 managed component (`managed_components/lvgl__lvgl/src/font/`). Enabling it requires one line in `sdkconfig.defaults`:

```
CONFIG_LV_FONT_SOURCE_HAN_SANS_SC_14_CJK=y
```

Flash impact: ~120 KB. Current firmware: 1.3 MB of 15 MB partition (9% used). After change: ~1.42 MB (~9.5%).

## Components Changed

### New: `components/ui/src/ui_fonts.c` + `components/ui/include/ui/ui_fonts.h`

```c
// ui_fonts.h
#pragma once
#include "lvgl.h"

extern lv_font_t g_font_14_cjk;
void ui_fonts_init(void);
```

```c
// ui_fonts.c
#include <string.h>
#include "lvgl.h"
#include "ui/ui_fonts.h"

lv_font_t g_font_14_cjk;

void ui_fonts_init(void) {
    memcpy(&g_font_14_cjk, &lv_font_montserrat_14, sizeof(lv_font_t));
    g_font_14_cjk.fallback = &lv_font_source_han_sans_sc_14_cjk;
}
```

`ui_fonts_init()` must be called before `ui_manager_init()`.

### Modified: `components/ui/src/ui_manager.c`

Three font assignments changed:

1. `s_main_content_label`: `&lv_font_montserrat_14` → `&g_font_14_cjk`
2. `s_transcript_label`: `&lv_font_unscii_8` → `&g_font_14_cjk`
3. `s_approval_hint`: add explicit `lv_obj_set_style_text_font(s_approval_hint, &g_font_14_cjk, 0)`

### Modified: `components/ui/CMakeLists.txt`

Add `src/ui_fonts.c` to the source list.

### Modified: `sdkconfig.defaults`

Add `CONFIG_LV_FONT_SOURCE_HAN_SANS_SC_14_CJK=y`.

## Layout Notes

`s_transcript_label` switches from `lv_font_unscii_8` (8 px) to `g_font_14_cjk` (14 px). The transcript container is 58 px tall — approximately 3 lines visible. The label uses `LV_LABEL_LONG_SCROLL_CIRCULAR` mode, so content still scrolls; line capacity reduction is acceptable.

## Call Site

`main.c` or wherever `ui_manager_init()` is called:

```c
ui_fonts_init();      // must precede ui_manager_init
ui_manager_init();
```
