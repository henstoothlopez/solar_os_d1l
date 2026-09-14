#include "d1l_expander.h"

#include "esp_log.h"
#include "tca9535.h"

#define D1L_EXPANDER_I2C_BUS "i2c0"   /* must match the manifest's [[buses]] name */
#define D1L_EXPANDER_ADDR_PRIMARY 0x20
#define D1L_EXPANDER_ADDR_FALLBACK 0x39

static const char *TAG = "d1l_expander";
static tca9535_t expander;
static bool expander_ready;

static esp_err_t ensure_ready(void)
{
    if (expander_ready) {
        return ESP_OK;
    }
    esp_err_t err = tca9535_init(&expander, D1L_EXPANDER_I2C_BUS,
                                 D1L_EXPANDER_ADDR_PRIMARY,
                                 D1L_EXPANDER_ADDR_FALLBACK);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TCA9535 init failed: %s", esp_err_to_name(err));
        return err;
    }
    expander_ready = true;
    return ESP_OK;
}

esp_err_t d1l_expander_pulse(uint8_t pin, uint32_t low_us)
{
    esp_err_t err = ensure_ready();
    if (err != ESP_OK) {
        return err;
    }
    return tca9535_pulse_low(&expander, pin, low_us);
}

esp_err_t d1l_expander_write(uint8_t pin, bool level)
{
    esp_err_t err = ensure_ready();
    if (err != ESP_OK) {
        return err;
    }
    err = tca9535_set_pin_mode(&expander, pin, true);
    if (err != ESP_OK) {
        return err;
    }
    return tca9535_write_pin(&expander, pin, level);
}

esp_err_t d1l_expander_write_fast(uint8_t pin, bool level)
{
    esp_err_t err = ensure_ready();
    if (err != ESP_OK) {
        return err;
    }
    return tca9535_write_pin(&expander, pin, level);
}
