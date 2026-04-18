#pragma once
#include "esp_err.h"
#include "buddy_hal/agent_events.h"
#include "state_machine.h"

esp_err_t agent_core_init(sm_handle_t sm);
esp_err_t agent_core_start(void);
esp_err_t agent_core_stop(void);

esp_err_t agent_core_post_event(const agent_event_t *evt);
