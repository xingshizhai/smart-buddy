#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "buddy_hal/hal_audio.h"
#include "buddy_hal/hal.h"

#define TAG "AUDIO"

typedef struct {
    esp_codec_dev_handle_t spk;
    esp_codec_dev_handle_t mic;
} audio_priv_t;

static hal_audio_t  s_audio;
static audio_priv_t s_priv;

static esp_err_t audio_init(hal_audio_t *audio, const hal_audio_cfg_t *cfg)
{
    bsp_audio_init(NULL);

    audio_priv_t *p = (audio_priv_t *)audio->priv;
    p->spk = bsp_audio_codec_speaker_init();
    p->mic = bsp_audio_codec_microphone_init();

    esp_codec_dev_sample_info_t info = {
        .sample_rate  = cfg ? cfg->sample_rate  : 16000,
        .bits_per_sample = cfg ? cfg->bits_per_sample : 16,
        .channel      = cfg ? cfg->channels     : 1,
    };
    if (p->spk) esp_codec_dev_open(p->spk, &info);
    if (p->mic) esp_codec_dev_open(p->mic, &info);
    return ESP_OK;
}

static esp_err_t audio_set_volume(hal_audio_t *audio, uint8_t vol_pct)
{
    audio_priv_t *p = (audio_priv_t *)audio->priv;
    if (p->spk) esp_codec_dev_set_out_vol(p->spk, vol_pct);
    return ESP_OK;
}

static esp_err_t audio_play(hal_audio_t *audio, const int16_t *samples, size_t n)
{
    audio_priv_t *p = (audio_priv_t *)audio->priv;
    if (!p->spk) return ESP_ERR_INVALID_STATE;
    return esp_codec_dev_write(p->spk, (void *)samples, n * sizeof(int16_t));
}

static esp_err_t audio_record_read(hal_audio_t *audio, int16_t *buf, size_t n, size_t *got)
{
    audio_priv_t *p = (audio_priv_t *)audio->priv;
    if (!p->mic) return ESP_ERR_INVALID_STATE;
    esp_err_t r = esp_codec_dev_read(p->mic, buf, n * sizeof(int16_t));
    if (got) *got = n;
    return r;
}

esp_err_t hal_audio_create(const hal_audio_cfg_t *cfg, hal_audio_t **out)
{
    s_audio.init         = audio_init;
    s_audio.set_volume   = audio_set_volume;
    s_audio.play         = audio_play;
    s_audio.record_read  = audio_record_read;
    s_audio.priv         = &s_priv;

    audio_init(&s_audio, cfg);
    *out = &s_audio;
    ESP_LOGI(TAG, "audio ready");
    return ESP_OK;
}

void hal_audio_destroy(hal_audio_t *audio) { }
