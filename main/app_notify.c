#include "audio_manager.h"
#include "state_machine.h"
#include "app_notify.h"

void app_notify_init(void)
{
    /* Pre-warm the alert tone buffer in audio_manager. */
    audio_manager_play_alert_preview();
    audio_manager_play_stop();
}

void app_notify_on_state_change(sm_state_t new_state, sm_state_t old_state)
{
    (void)old_state;
    if (new_state == SM_STATE_ATTENTION)
        audio_manager_play_alert_preview();
}
