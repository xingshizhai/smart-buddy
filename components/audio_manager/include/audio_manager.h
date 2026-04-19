#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "buddy_hal/hal_audio.h"

/*
 * Called from the record task each time a ~32 ms PCM chunk is ready.
 * Do NOT block or allocate inside this callback; copy the data if needed.
 */
typedef void (*audio_chunk_cb_t)(const int16_t *pcm, size_t n_samples, void *ctx);

/*
 * Called once when a push-to-talk session ends, with the full recording.
 * The buffer is valid only for the duration of the callback; copy if needed.
 * n_samples == 0 if recording was too short or the buffer overflowed.
 */
typedef void (*audio_record_done_cb_t)(const int16_t *pcm, size_t n_samples,
                                        uint32_t sample_rate, void *ctx);

esp_err_t audio_manager_init(hal_audio_t *audio);
esp_err_t audio_manager_start(void);

/* Recording ---------------------------------------------------------------- */

esp_err_t audio_manager_record_start(void);
esp_err_t audio_manager_record_stop(void);   /* fires record_done_cb */
bool      audio_manager_is_recording(void);

/*
 * Raw chunk callback — called for every ~32 ms chunk while recording.
 * Useful for streaming. Set before calling record_start.
 */
void audio_manager_set_chunk_cb(audio_chunk_cb_t cb, void *ctx);

/*
 * Session-complete callback — called after record_stop with the full buffer.
 * The buffer holds up to AUDIO_MANAGER_MAX_RECORD_S seconds.
 */
void audio_manager_set_record_done_cb(audio_record_done_cb_t cb, void *ctx);

/* Playback ----------------------------------------------------------------- */

/*
 * Queue raw 16-bit PCM for playback. Data is copied internally.
 * Queuing multiple calls fills a pipeline; use play_stop() to abort.
 */
esp_err_t audio_manager_play_raw(const int16_t *pcm, size_t n_samples);

/* Stop and discard any queued playback. */
esp_err_t audio_manager_play_stop(void);

bool      audio_manager_is_playing(void);

/* Volume (0–100). Applied immediately to the speaker codec. */
esp_err_t audio_manager_set_volume(uint8_t vol_pct);
