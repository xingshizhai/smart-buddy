#pragma once
#include "state_machine.h"

void app_notify_init(void);
void app_notify_on_state_change(sm_state_t new_state, sm_state_t old_state);
