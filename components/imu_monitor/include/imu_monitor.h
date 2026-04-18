#pragma once
#include "esp_err.h"
#include "buddy_hal/hal_imu.h"

esp_err_t imu_monitor_start(hal_imu_t *imu);
esp_err_t imu_monitor_stop(void);
