#include "ft6336.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "solar_os_buses.h"

#define FT6336_REG_TOUCH_STATUS 0x02U
#define FT6336_REG_CHIP_ID 0xa3U

static const char *TAG = "ft6336";
static bool ft6336_ready;
static char ft6336_i2c_bus[SOLAR_OS_BUS_NAME_MAX];
static uint8_t ft6336_address;

/*
 * CHANGED 2026-09-01 for the D1L port (SolarOS-D1L-Port-Roadmap.md
 * section 5): two behavior changes, both additive and backward-compatible
 * with every other board already using this driver.
 *
 *  1. Dual-address probe. Some FT6336U boards answer at 0x38, others at
 *     0x48 (board-revision dependent — same fact independently confirmed
 *     three times during the Launcher port on this same D1L hardware).
 *     `address_fallback` is new; pass 0 to keep old single-address
 *     behavior exactly as before.
 *
 *  2. `reset_pin < 0` is no longer rejected as invalid. Passing -1 means
 *     "the caller already reset the controller before calling this" — for
 *     boards where touch RST is gated through an IO expander (e.g. D1L's
 *     TCA9535) rather than wired to a raw ESP32 GPIO, there is no local
 *     pin for this driver to toggle. Every existing caller that passes a
 *     real GPIO number keeps behaving exactly as before; this only adds a
 *     new legal value, it does not change existing behavior for anyone.
 */
esp_err_t ft6336_init(const char *i2c_bus,
                      uint8_t address,
                      uint8_t address_fallback,
                      int reset_pin,
                      int irq_pin)
{
    if (i2c_bus == NULL || i2c_bus[0] == '\0' ||
        (reset_pin != -1 && (reset_pin < 0 || reset_pin >= 64)) ||
        irq_pin < 0 || irq_pin >= 64) {
        return ESP_ERR_INVALID_ARG;
    }
    if (ft6336_ready) {
        return strcmp(ft6336_i2c_bus, i2c_bus) == 0 &&
                (ft6336_address == address || ft6336_address == address_fallback)
            ? ESP_OK
            : ESP_ERR_INVALID_STATE;
    }

    if (reset_pin >= 0) {
        const gpio_config_t output = {
            .pin_bit_mask = 1ULL << (uint32_t)reset_pin,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&output), TAG, "reset pin config failed");
        ESP_RETURN_ON_ERROR(gpio_set_level(reset_pin, 0),
                            TAG,
                            "reset low failed");
        vTaskDelay(pdMS_TO_TICKS(10));
        ESP_RETURN_ON_ERROR(gpio_set_level(reset_pin, 1),
                            TAG,
                            "reset high failed");
        vTaskDelay(pdMS_TO_TICKS(500));
    } else {
        /* Reset already handled externally (e.g. via an IO expander before
         * this function was called). Give the controller the same settle
         * time it would have gotten from a local reset pulse. */
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    const gpio_config_t input = {
        .pin_bit_mask = 1ULL << (uint32_t)irq_pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&input), TAG, "interrupt pin config failed");

    uint8_t chip_id = 0;
    esp_err_t err = solar_os_bus_i2c_read_reg(i2c_bus, address,
                                              FT6336_REG_CHIP_ID,
                                              &chip_id, sizeof(chip_id));
    uint8_t resolved_address = address;
    if (err != ESP_OK && address_fallback != 0) {
        ESP_LOGW(TAG, "No response at 0x%02X, probing fallback 0x%02X",
                 address, address_fallback);
        err = solar_os_bus_i2c_read_reg(i2c_bus, address_fallback,
                                        FT6336_REG_CHIP_ID,
                                        &chip_id, sizeof(chip_id));
        resolved_address = address_fallback;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "chip ID read failed at both addresses");

    strlcpy(ft6336_i2c_bus, i2c_bus, sizeof(ft6336_i2c_bus));
    ft6336_address = resolved_address;
    ft6336_ready = true;
    return ESP_OK;
}

esp_err_t ft6336_read(ft6336_sample_t *sample)
{
    if (sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ft6336_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t data[6] = {0};
    ESP_RETURN_ON_ERROR(solar_os_bus_i2c_read_reg(ft6336_i2c_bus,
                                                 ft6336_address,
                                                 FT6336_REG_TOUCH_STATUS,
                                                 data,
                                                 sizeof(data)),
                        TAG,
                        "touch read failed");
    memset(sample, 0, sizeof(*sample));
    const uint8_t count = data[0] & 0x0fU;
    if (count == 0) {
        return ESP_OK;
    }

    sample->touched = true;
    sample->x = (uint16_t)(((uint16_t)(data[1] & 0x0fU) << 8) | data[2]);
    sample->y = (uint16_t)(((uint16_t)(data[3] & 0x0fU) << 8) | data[4]);
    sample->id = data[3] >> 4;
    return ESP_OK;
}

void ft6336_deinit(void)
{
    ft6336_ready = false;
    ft6336_i2c_bus[0] = '\0';
    ft6336_address = 0;
}
