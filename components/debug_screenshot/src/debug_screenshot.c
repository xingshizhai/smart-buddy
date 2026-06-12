#include "sdkconfig.h"
#include "debug_screenshot.h"

#if CONFIG_DEBUG_SCREENSHOT_ENABLED

#include <string.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "draw/lv_draw_buf_private.h"
#include "esp_lvgl_port.h"
#include "lwip/sockets.h"

#define TAG "SCRSHOT"

#define SCREEN_W 320
#define SCREEN_H 240

/* Wire format sent after a 'S' request: this header, then data_len bytes
 * of raw RGB565 pixel data (little-endian, row-major, `stride` bytes/row). */
typedef struct __attribute__((packed)) {
    uint16_t w;
    uint16_t h;
    uint16_t stride;
    uint16_t reserved;
    uint32_t data_len;
} screenshot_header_t;

static lv_draw_buf_t *s_draw_buf;

/* The 320x240 RGB565 snapshot (150KB) is too large for LVGL's internal
 * 64KB memory pool, so this buffer is allocated directly from PSRAM and
 * reused for every snapshot. */
static void *psram_buf_malloc(size_t size, lv_color_format_t cf)
{
    (void)cf;
    return heap_caps_malloc(size + LV_DRAW_BUF_ALIGN - 1, MALLOC_CAP_SPIRAM);
}

static void psram_buf_free(void *buf)
{
    heap_caps_free(buf);
}

static esp_err_t send_all(int sock, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    while (len > 0) {
        int sent = send(sock, p, len, 0);
        if (sent <= 0) return ESP_FAIL;
        p += (size_t)sent;
        len -= (size_t)sent;
    }
    return ESP_OK;
}

static void handle_client(int sock)
{
    uint8_t cmd = 0;
    int n = recv(sock, &cmd, 1, 0);
    if (n != 1 || cmd != 'S') return;

    if (!lvgl_port_lock(200)) {
        ESP_LOGW(TAG, "lvgl lock timeout, skipping snapshot");
        return;
    }
    lv_result_t res = lv_snapshot_take_to_draw_buf(lv_scr_act(), LV_COLOR_FORMAT_RGB565, s_draw_buf);
    lvgl_port_unlock();

    if (res != LV_RESULT_OK) {
        ESP_LOGW(TAG, "snapshot failed");
        return;
    }

    screenshot_header_t hdr = {
        .w        = (uint16_t)s_draw_buf->header.w,
        .h        = (uint16_t)s_draw_buf->header.h,
        .stride   = (uint16_t)s_draw_buf->header.stride,
        .reserved = 0,
        .data_len = s_draw_buf->data_size,
    };

    if (send_all(sock, &hdr, sizeof(hdr)) != ESP_OK) return;
    send_all(sock, s_draw_buf->data, s_draw_buf->data_size);
}

static void screenshot_task(void *arg)
{
    int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "socket() failed: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port        = htons((uint16_t)CONFIG_DEBUG_SCREENSHOT_PORT),
    };

    if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "bind() failed: errno %d", errno);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    if (listen(listen_sock, 1) != 0) {
        ESP_LOGE(TAG, "listen() failed: errno %d", errno);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "screenshot server listening on port %d", CONFIG_DEBUG_SCREENSHOT_PORT);

    for (;;) {
        struct sockaddr_in src_addr;
        socklen_t addr_len = sizeof(src_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&src_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGW(TAG, "accept() failed: errno %d", errno);
            continue;
        }
        handle_client(sock);
        close(sock);
    }
}

esp_err_t debug_screenshot_start(void)
{
    static lv_draw_buf_handlers_t handlers;
    lv_draw_buf_handlers_init(&handlers, psram_buf_malloc, psram_buf_free,
                               NULL, NULL, NULL, NULL, NULL);

    s_draw_buf = lv_draw_buf_create_ex(&handlers, SCREEN_W, SCREEN_H, LV_COLOR_FORMAT_RGB565, 0);
    if (!s_draw_buf) {
        ESP_LOGE(TAG, "failed to allocate %dx%d snapshot buffer in PSRAM", SCREEN_W, SCREEN_H);
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(screenshot_task, "screenshot", 4096, NULL, 3, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

#else /* !CONFIG_DEBUG_SCREENSHOT_ENABLED */

esp_err_t debug_screenshot_start(void)
{
    return ESP_OK;
}

#endif
