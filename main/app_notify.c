#include "audio_manager.h"
#include "state_machine.h"
#include "app_notify.h"

void app_notify_init(void)
{
    /* Alert tone buffer is built lazily on first use in audio_manager.
     * Do not pre-warm here — BT controller init needs that heap. */
}

void app_notify_on_state_change(sm_state_t new_state, sm_state_t old_state)
{
    (void)old_state;
    if (new_state == SM_STATE_ATTENTION)
        audio_manager_play_alert_preview();
}
