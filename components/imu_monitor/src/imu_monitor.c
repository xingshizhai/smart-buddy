#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "imu_monitor.h"
#include "agent_core.h"
#include "buddy_hal/agent_events.h"

#define TAG          "IMU"

#ifndef CONFIG_IMU_SHAKE_THRESHOLD_MG
#define CONFIG_IMU_SHAKE_THRESHOLD_MG   1500
#endif
#ifndef CONFIG_IMU_FACE_DOWN_THRESHOLD_MG
#define CONFIG_IMU_FACE_DOWN_THRESHOLD_MG 850
#endif
#define POLL_HZ      50
#define POLL_MS      (1000 / POLL_HZ)

#define SHAKE_THRESHOLD_MPS2   ((float)(CONFIG_IMU_SHAKE_THRESHOLD_MG) * 9.81f / 1000.0f)
#define FACE_DOWN_Z_G          (-(float)(CONFIG_IMU_FACE_DOWN_THRESHOLD_MG) / 1000.0f)

static hal_imu_t *s_imu      = NULL;
static TaskHandle_t s_task   = NULL;

static void post_gesture(imu_gesture_t g)
{
    agent_event_t evt = {
        .type = AGENT_EVT_IMU_GESTURE,
        .data.imu.gesture = g,
        .timestamp_us = esp_timer_get_time(),
    };
    agent_core_post_event(&evt);
}

static void imu_task(void *arg)
{
    TickType_t last    = xTaskGetTickCount();
    float      prev_mag = 0.0f;
    bool       face_down = false;

    for (;;) {
        hal_accel_t a;
        if (s_imu->read_accel(s_imu, &a) == ESP_OK) {
            float mag = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
            float delta = fabsf(mag - prev_mag);
            prev_mag = mag;

            if (delta > SHAKE_THRESHOLD_MPS2) {
                post_gesture(IMU_GESTURE_SHAKE);
                ESP_LOGD(TAG, "shake detected, delta=%.2f", delta);
            }

            float z_g = a.z / 9.81f;
            bool now_down = (z_g < FACE_DOWN_Z_G);
            if (now_down != face_down) {
                face_down = now_down;
                post_gesture(face_down ? IMU_GESTURE_FACE_DOWN : IMU_GESTURE_FACE_UP);
                ESP_LOGD(TAG, "face %s", face_down ? "down" : "up");
            }
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(POLL_MS));
    }
}

esp_err_t imu_monitor_start(hal_imu_t *imu)
{
    s_imu = imu;
    if (imu->init(imu) != ESP_OK) {
        ESP_LOGW(TAG, "IMU init failed, skipping monitor");
        return ESP_OK;
    }
    BaseType_t r = xTaskCreatePinnedToCore(imu_task, "imu_monitor",
                                            4096, NULL, 3, &s_task, 1);
    return r == pdPASS ? ESP_OK : ESP_FAIL;
}

esp_err_t imu_monitor_stop(void)
{
    if (s_task) { vTaskDelete(s_task); s_task = NULL; }
    return ESP_OK;
}
