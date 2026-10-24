/* solar_os_d1l_keys.c  (delivered 2026-10-23)
 *
 * SenseCAP Indicator D1L keyboard driver for SolarOS.
 *
 * How it works (same protocol the Launcher port uses, proven on hardware):
 *   1. We own UART1 on ESP32 GPIO19 (TX) / GPIO20 (RX) at 115200 baud.
 *   2. Every ~50 ms we send "keys\n" to the RP2040.
 *   3. The RP2040 replies "OK <n> bytes\r\n" followed by exactly <n> raw key
 *      bytes (no trailing newline). n is 0 when no key is waiting.
 *   4. Each key byte is decoded with the upstream CardKB decoder and sent
 *      into the SolarOS input service as a keyboard character.
 *
 * Notes:
 *   - Replies are synchronous (request, then wait). Nothing is pushed by the
 *     RP2040 on its own, so the shared link stays simple.
 *   - GPIO43/44 are the CH340 console UART. They are NOT used here.
 *   - Log lines start with "d1l_keys:" so they are easy to find in the boot
 *     log:  "link OK" means the UART link works; "key 0x.. -> 0x.." lines
 *     show the first keys received.
 */

#include "solar_os_d1l_keys.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "solar_os_cardkb_codec.h"
#include "solar_os_input.h"
#include "solar_os_task.h"

#define D1L_KEYS_UART UART_NUM_1
#define D1L_KEYS_PIN_TX 19
#define D1L_KEYS_PIN_RX 20
#define D1L_KEYS_BAUD 115200
#define D1L_KEYS_RX_BUFFER 256
#define D1L_KEYS_POLL_MS 50U
#define D1L_KEYS_REPLY_TIMEOUT_MS 200U
#define D1L_KEYS_LINE_MAX 32U
#define D1L_KEYS_BATCH_MAX 16U
#define D1L_KEYS_TASK_STACK 4096U
#define D1L_KEYS_TASK_PRIORITY (tskIDLE_PRIORITY + 1)
#define D1L_KEYS_LOG_FIRST_KEYS 32U

typedef struct {
    bool active;
    bool uart_installed;
    volatile bool stop_requested;
    volatile bool worker_done;
    char name[SOLAR_OS_EXPANSION_DEVICE_NAME_MAX];
    solar_os_input_source_t input_source;
    TaskHandle_t worker_task;
    uint32_t keys;
    uint32_t unsupported;
    uint32_t dropped;
    uint32_t link_errors;
} d1l_keys_device_t;

static const char *TAG = "d1l_keys";
static d1l_keys_device_t s_dev;

/* Read one byte, giving up when the tick deadline passes. */
static bool read_byte(uint8_t *out, TickType_t deadline)
{
    const int32_t left = (int32_t)(deadline - xTaskGetTickCount());
    if (left <= 0) {
        return false;
    }
    return uart_read_bytes(D1L_KEYS_UART, out, 1, (TickType_t)left) == 1;
}

/* Ask the RP2040 for pending key bytes. */
static esp_err_t request_keys(uint8_t *batch, size_t capacity, size_t *count)
{
    static const char cmd[] = "keys\n";
    char line[D1L_KEYS_LINE_MAX];
    size_t len = 0;
    unsigned n = 0;

    *count = 0;

    /* Throw away anything stale (for example the RP2040 boot banner). */
    uart_flush_input(D1L_KEYS_UART);
    if (uart_write_bytes(D1L_KEYS_UART, cmd, sizeof(cmd) - 1) != (int)(sizeof(cmd) - 1)) {
        return ESP_FAIL;
    }

    const TickType_t deadline =
        xTaskGetTickCount() + pdMS_TO_TICKS(D1L_KEYS_REPLY_TIMEOUT_MS);

    /* Header line: "OK <n> bytes" */
    for (;;) {
        uint8_t c = 0;
        if (!read_byte(&c, deadline)) {
            return ESP_ERR_TIMEOUT;
        }
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            if (len == 0) {
                continue; /* ignore blank lines before the header */
            }
            break;
        }
        if (len >= sizeof(line) - 1) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        line[len++] = (char)c;
    }
    line[len] = '\0';

    if (sscanf(line, "OK %u bytes", &n) != 1) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (n > capacity) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* Payload: exactly n raw bytes. */
    for (unsigned i = 0; i < n; i++) {
        if (!read_byte(&batch[i], deadline)) {
            return ESP_ERR_TIMEOUT;
        }
    }
    *count = n;
    return ESP_OK;
}

/* Same "documented values only" filter the Launcher port uses, so a stray
 * byte from the RP2040 side cannot turn into a random keypress. */
static bool is_documented_key(uint8_t k)
{
    return k == 0x08 || k == 0x0A || k == 0x0D || k == 0x1B ||
           (k >= 0x20 && k <= 0x7E) || (k >= 0xB4 && k <= 0xB7);
}

static void d1l_keys_worker(void *arg)
{
    d1l_keys_device_t *dev = arg;
    bool link_ok = false;
    uint32_t fails = 0;
    uint32_t logged_keys = 0;

    while (!dev->stop_requested) {
        uint8_t batch[D1L_KEYS_BATCH_MAX];
        size_t count = 0;
        const esp_err_t err = request_keys(batch, sizeof(batch), &count);

        if (err == ESP_OK) {
            if (!link_ok) {
                link_ok = true;
                ESP_LOGI(TAG,
                         "link OK: RP2040 answered 'keys' (UART%d TX=GPIO%d RX=GPIO%d %d baud)",
                         (int)D1L_KEYS_UART,
                         D1L_KEYS_PIN_TX,
                         D1L_KEYS_PIN_RX,
                         D1L_KEYS_BAUD);
            }
            fails = 0;
            for (size_t i = 0; i < count; i++) {
                uint8_t key = 0;
                if (!is_documented_key(batch[i]) ||
                    !solar_os_cardkb_decode(batch[i], &key)) {
                    dev->unsupported++;
                    continue;
                }
                if (solar_os_input_write_char(dev->input_source, (char)key) != ESP_OK) {
                    dev->dropped++;
                    continue;
                }
                dev->keys++;
                if (logged_keys < D1L_KEYS_LOG_FIRST_KEYS) {
                    logged_keys++;
                    ESP_LOGI(TAG, "key 0x%02x -> 0x%02x", batch[i], key);
                }
            }
        } else {
            dev->link_errors++;
            fails++;
            if (fails == 1 || fails % 100 == 0) {
                ESP_LOGW(TAG,
                         "RP2040 not answering 'keys' properly (%s), failures in a row: %lu",
                         esp_err_to_name(err),
                         (unsigned long)fails);
            }
            link_ok = false;
        }

        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(D1L_KEYS_POLL_MS));
    }

    dev->worker_done = true;
    solar_os_task_delete_internal(NULL);
}

static void cleanup(d1l_keys_device_t *dev)
{
    if (dev->input_source != SOLAR_OS_INPUT_SOURCE_INVALID) {
        solar_os_input_source_close(dev->input_source);
    }
    if (dev->uart_installed) {
        uart_driver_delete(D1L_KEYS_UART);
    }
    memset(dev, 0, sizeof(*dev));
}

esp_err_t solar_os_d1l_keys_attach(const char *name,
                                   const solar_os_expansion_binding_t *bindings,
                                   size_t binding_count)
{
    (void)bindings;
    (void)binding_count;

    if (name == NULL || name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_dev.active) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(&s_dev, 0, sizeof(s_dev));
    strlcpy(s_dev.name, name, sizeof(s_dev.name));

    const uart_config_t cfg = {
        .baud_rate = D1L_KEYS_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(D1L_KEYS_UART, D1L_KEYS_RX_BUFFER, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        memset(&s_dev, 0, sizeof(s_dev));
        return err;
    }
    s_dev.uart_installed = true;

    err = uart_param_config(D1L_KEYS_UART, &cfg);
    if (err == ESP_OK) {
        err = uart_set_pin(D1L_KEYS_UART,
                           D1L_KEYS_PIN_TX,
                           D1L_KEYS_PIN_RX,
                           UART_PIN_NO_CHANGE,
                           UART_PIN_NO_CHANGE);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART setup failed: %s", esp_err_to_name(err));
        cleanup(&s_dev);
        return err;
    }

    err = solar_os_input_keyboard_source_open(s_dev.name, true, &s_dev.input_source);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "input source open failed: %s", esp_err_to_name(err));
        cleanup(&s_dev);
        return err;
    }

    s_dev.active = true;
    if (solar_os_task_create_pinned_internal(d1l_keys_worker,
                                             s_dev.name,
                                             D1L_KEYS_TASK_STACK,
                                             &s_dev,
                                             D1L_KEYS_TASK_PRIORITY,
                                             &s_dev.worker_task,
                                             tskNO_AFFINITY,
                                             SOLAR_OS_TASK_ROLE_BACKGROUND) != pdPASS) {
        ESP_LOGE(TAG, "could not start worker task");
        cleanup(&s_dev);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG,
             "%s attached: waiting for RP2040 on UART%d (TX=GPIO%d RX=GPIO%d)",
             name,
             (int)D1L_KEYS_UART,
             D1L_KEYS_PIN_TX,
             D1L_KEYS_PIN_RX);
    return ESP_OK;
}

esp_err_t solar_os_d1l_keys_detach(const char *name)
{
    if (!s_dev.active || name == NULL || strcmp(s_dev.name, name) != 0) {
        return ESP_ERR_NOT_FOUND;
    }

    s_dev.stop_requested = true;
    if (s_dev.worker_task != NULL) {
        (void)xTaskNotifyGive(s_dev.worker_task);
    }
    if (!solar_os_task_wait_done(s_dev.worker_task,
                                 &s_dev.worker_done,
                                 SOLAR_OS_TASK_STOP_WAIT_MS)) {
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGI(TAG,
             "%s detached: %lu keys, %lu unsupported, %lu dropped, %lu link errors",
             name,
             (unsigned long)s_dev.keys,
             (unsigned long)s_dev.unsupported,
             (unsigned long)s_dev.dropped,
             (unsigned long)s_dev.link_errors);
    cleanup(&s_dev);
    return ESP_OK;
}
