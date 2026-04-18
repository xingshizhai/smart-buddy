#pragma once
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    uint32_t tokens_total;
    uint32_t tokens_today;
    uint32_t approvals_granted;
    uint32_t approvals_denied;
    uint32_t sessions_count;
    uint32_t milestone_next;
} agent_stats_t;

esp_err_t     agent_stats_init(void);
agent_stats_t agent_stats_get(void);
void          agent_stats_update_tokens(uint32_t total, uint32_t today);
void          agent_stats_record_approval(bool approved);
void          agent_stats_flush(void);
bool          agent_stats_check_milestone(uint32_t new_total);
