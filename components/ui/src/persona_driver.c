#include "ui/persona.h"
#include "lvgl.h"
#include "esp_random.h"

#define DRIVER_TICK_MS 150

static persona_frame_cb_t s_cb;
static void              *s_ctx;
static int                s_persona_idx;
static sm_state_t         s_state;
static int                s_frame_idx;
static uint32_t           s_elapsed_ms;
static lv_timer_t        *s_timer;

static void push_frame(void)
{
    if (!s_cb) return;
    const persona_t      *p = g_personas[s_persona_idx];
    const persona_anim_t *a = &p->anims[s_state];
    s_cb(a->frames[s_frame_idx], s_ctx);
}

static void timer_cb(lv_timer_t *t)
{
    const persona_t      *p = g_personas[s_persona_idx];
    const persona_anim_t *a = &p->anims[s_state];

    s_elapsed_ms += DRIVER_TICK_MS;
    if (s_elapsed_ms < a->frame_ms) return;

    s_elapsed_ms = 0;
    s_frame_idx++;
    if (!a->frames[s_frame_idx]) s_frame_idx = 0;
    push_frame();
}

void persona_driver_init(persona_frame_cb_t cb, void *ctx)
{
    s_cb         = cb;
    s_ctx        = ctx;
    s_persona_idx = (int)(esp_random() % (uint32_t)g_persona_count);
    s_state      = SM_STATE_SLEEP;
    s_frame_idx  = 0;
    s_elapsed_ms = 0;

    s_timer = lv_timer_create(timer_cb, DRIVER_TICK_MS, NULL);
    push_frame();
}

void persona_driver_deinit(void)
{
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
}

void persona_driver_set_state(sm_state_t state)
{
    if (state >= SM_STATE_MAX) return;
    s_state      = state;
    s_frame_idx  = 0;
    s_elapsed_ms = 0;
    push_frame();
}

void persona_driver_set_persona(int idx)
{
    if (idx < 0 || idx >= g_persona_count) return;
    s_persona_idx = idx;
    s_frame_idx   = 0;
    s_elapsed_ms  = 0;
    push_frame();
}

int persona_driver_get_persona(void)
{
    return s_persona_idx;
}
