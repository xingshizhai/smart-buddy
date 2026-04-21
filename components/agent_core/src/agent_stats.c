#include <string.h>
#include "esp_log.h"
#include "buddy_hal/hal_storage.h"
#include "agent_stats.h"

#define TAG "STATS"

#ifndef CONFIG_PROTO_TOKEN_MILESTONE
#define CONFIG_PROTO_TOKEN_MILESTONE 50000
#endif

static agent_stats_t s_stats = {0};

esp_err_t agent_stats_init(void)
{
    hal_storage_get_u32("tok_total",  &s_stats.tokens_total,    0);
    hal_storage_get_u32("tok_today",  &s_stats.tokens_today,    0);
    hal_storage_get_u32("appr_ok",    &s_stats.approvals_granted, 0);
    hal_storage_get_u32("appr_deny",  &s_stats.approvals_denied,  0);
    hal_storage_get_u32("sess_cnt",   &s_stats.sessions_count,   0);
    hal_storage_get_u32("mile_next",  &s_stats.milestone_next,   CONFIG_PROTO_TOKEN_MILESTONE);
    ESP_LOGI(TAG, "loaded: total=%lu today=%lu", s_stats.tokens_total, s_stats.tokens_today);
    return ESP_OK;
}

agent_stats_t agent_stats_get(void) { return s_stats; }

void agent_stats_update_tokens(uint32_t total, uint32_t today)
{
    s_stats.tokens_total = total;
    s_stats.tokens_today = today;
}

void agent_stats_record_approval(bool approved)
{
    if (approved) s_stats.approvals_granted++;
    else          s_stats.approvals_denied++;
}

void agent_stats_record_session_start(void)
{
    s_stats.sessions_count++;
}

bool agent_stats_check_milestone(uint32_t new_total)
{
    if (new_total >= s_stats.milestone_next) {
        s_stats.milestone_next += CONFIG_PROTO_TOKEN_MILESTONE;
        return true;
    }
    return false;
}

void agent_stats_flush(void)
{
    hal_storage_set_u32("tok_total",  s_stats.tokens_total);
    hal_storage_set_u32("tok_today",  s_stats.tokens_today);
    hal_storage_set_u32("appr_ok",    s_stats.approvals_granted);
    hal_storage_set_u32("appr_deny",  s_stats.approvals_denied);
    hal_storage_set_u32("sess_cnt",   s_stats.sessions_count);
    hal_storage_set_u32("mile_next",  s_stats.milestone_next);
}
