#pragma once
#include <stdint.h>
#include "state_machine.h"

/* One animation: a NULL-terminated list of ASCII-art frame strings + ms/frame. */
typedef struct {
    const char * const *frames;
    uint16_t            frame_ms;
} persona_anim_t;

/* One species: a display name and one animation per state. */
typedef struct {
    const char    *name;
    persona_anim_t anims[SM_STATE_MAX];
} persona_t;

/* 18 species defined in persona_data.c */
extern const persona_t * const g_personas[];
extern const int                g_persona_count;  /* 18 */

/* Callback invoked on each frame advance (called from LVGL task context). */
typedef void (*persona_frame_cb_t)(const char *frame, void *ctx);

void persona_driver_init(persona_frame_cb_t cb, void *ctx);
void persona_driver_deinit(void);
void persona_driver_set_state(sm_state_t state);
void persona_driver_set_persona(int idx);
int  persona_driver_get_persona(void);
