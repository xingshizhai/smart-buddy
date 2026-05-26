#include "esp_log.h"
#include "esp_timer.h"
#include "bsp/esp-bsp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "icm42670.h"
#include "buddy_hal/hal_imu.h"
#include "buddy_hal/hal.h"

#define TAG "IMU"
#define IMU_FAIL_THRESHOLD 5
#define IMU_RECOVER_INTERVAL_US (2 * 1000 * 1000)

typedef struct {
    icm42670_handle_t handle;
    uint8_t fail_streak;
    bool suspended;
    int64_t next_recover_us;
} imu_priv_t;

static hal_imu_t  s_imu;
static imu_priv_t s_priv;

static esp_err_t imu_init(hal_imu_t *imu);

static bool imu_try_recover(hal_imu_t *imu)
{
    imu_priv_t *p = (imu_priv_t *)imu->priv;
    int64_t now = esp_timer_get_time();

    if (!p->suspended) {
        return true;
    }
    if (now < p->next_recover_us) {
        return false;
    }

    if (imu_init(imu) == ESP_OK) {
        p->suspended = false;
        p->fail_streak = 0;
        p->next_recover_us = 0;
        ESP_LOGI(TAG, "IMU recovered");
        return true;
    }

    p->next_recover_us = now + IMU_RECOVER_INTERVAL_US;
    return false;
}

static void imu_on_read_failure(hal_imu_t *imu, esp_err_t err)
{
    imu_priv_t *p = (imu_priv_t *)imu->priv;
    if (p->fail_streak < UINT8_MAX) {
        p->fail_streak++;
    }
    if (p->fail_streak >= IMU_FAIL_THRESHOLD && !p->suspended) {
        p->suspended = true;
        p->next_recover_us = esp_timer_get_time() + IMU_RECOVER_INTERVAL_US;
        ESP_LOGW(TAG, "IMU read failed repeatedly (%s), pausing reads for recovery", esp_err_to_name(err));
    }
}

static esp_err_t imu_init(hal_imu_t *imu)
{
    imu_priv_t *p = (imu_priv_t *)imu->priv;
    if (!p || !p->handle) {
        return ESP_ERR_INVALID_STATE;
    }

    icm42670_cfg_t cfg = {
        .acce_fs  = ACCE_FS_4G,
        .acce_odr = ACCE_ODR_400HZ,
        .gyro_fs  = GYRO_FS_500DPS,
        .gyro_odr = GYRO_ODR_400HZ,
    };
    esp_err_t ret = icm42670_config(p->handle, &cfg);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = icm42670_acce_set_pwr(p->handle, ACCE_PWR_LOWNOISE);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = icm42670_gyro_set_pwr(p->handle, GYRO_PWR_LOWNOISE);
    if (ret != ESP_OK) {
        return ret;
    }

    /* Give sensor time to leave standby before first sample. */
    vTaskDelay(pdMS_TO_TICKS(10));

    icm42670_value_t probe = {0};
    return icm42670_get_acce_value(p->handle, &probe);
}

static esp_err_t imu_read_accel(hal_imu_t *imu, hal_accel_t *out)
{
    imu_priv_t *p = (imu_priv_t *)imu->priv;
    if (!out || !p) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!imu_try_recover(imu)) {
        return ESP_ERR_INVALID_STATE;
    }

    icm42670_value_t val;
    esp_err_t r = icm42670_get_acce_value(p->handle, &val);
    if (r == ESP_OK) {
        p->fail_streak = 0;
        out->x = val.x;
        out->y = val.y;
        out->z = val.z;
    } else {
        imu_on_read_failure(imu, r);
    }
    return r;
}

static esp_err_t imu_read_gyro(hal_imu_t *imu, hal_gyro_t *out)
{
    imu_priv_t *p = (imu_priv_t *)imu->priv;
    if (!out || !p) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!imu_try_recover(imu)) {
        return ESP_ERR_INVALID_STATE;
    }

    icm42670_value_t val;
    esp_err_t r = icm42670_get_gyro_value(p->handle, &val);
    if (r == ESP_OK) {
        p->fail_streak = 0;
        out->x = val.x;
        out->y = val.y;
        out->z = val.z;
    } else {
        imu_on_read_failure(imu, r);
    }
    return r;
}

static esp_err_t imu_read_both(hal_imu_t *imu, hal_accel_t *a, hal_gyro_t *g)
{
    imu_read_accel(imu, a);
    return imu_read_gyro(imu, g);
}

esp_err_t hal_imu_create(hal_imu_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }

    bsp_i2c_init();
    i2c_master_bus_handle_t i2c_bus = bsp_i2c_get_handle();
    if (!i2c_bus) {
        ESP_LOGW(TAG, "I2C bus not ready, IMU disabled");
        return ESP_FAIL;
    }

    esp_err_t ret = icm42670_create(i2c_bus, ICM42670_I2C_ADDRESS, &s_priv.handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ICM42670 not found at 0x%02X, trying 0x%02X", ICM42670_I2C_ADDRESS, ICM42670_I2C_ADDRESS_1);
        ret = icm42670_create(i2c_bus, ICM42670_I2C_ADDRESS_1, &s_priv.handle);
    }

    if (ret != ESP_OK || !s_priv.handle) {
        ESP_LOGW(TAG, "IMU create failed (%s), IMU monitor will be skipped", esp_err_to_name(ret));
        return ret;
    }

    s_imu.init       = imu_init;
    s_imu.read_accel = imu_read_accel;
    s_imu.read_gyro  = imu_read_gyro;
    s_imu.read_both  = imu_read_both;
    s_imu.priv       = &s_priv;

    *out = &s_imu;
    ESP_LOGI(TAG, "IMU created");
    return ESP_OK;
}

void hal_imu_destroy(hal_imu_t *imu) { }
