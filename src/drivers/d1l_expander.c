/*
 * d1l_expander.c  --  version 2026-10-21
 *
 * CHANGE FROM PREVIOUS VERSION:
 *   ensure_ready() now takes a "lease" on the i2c0 bus BEFORE probing the
 *   TCA9535. Reason (from reading solar_os_buses.c): solar_os_bus_i2c_probe()
 *   goes through pin_ready_bus_owned(), which returns ESP_ERR_INVALID_STATE
 *   unless the bus is started AND at least one owner holds a lease on it.
 *   A bus only starts when something acquires a lease. Before this change
 *   nothing held a lease when the display attached, so the probe was refused
 *   and tca9535_init() reported it as "not found at 0x20 or 0x39".
 *
 *   The lease is kept for the life of the program once the expander is up.
 *   If init fails, the lease is released so repeated attempts do not pile up.
 */

#include "d1l_expander.h"

#include "esp_log.h"
#include "solar_os_buses.h"
#include "tca9535.h"

#define D1L_EXPANDER_I2C_BUS "i2c0"   /* must match the manifest's [[buses]] name */
#define D1L_EXPANDER_BUS_OWNER "d1l_expander"
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

    esp_err_t err = solar_os_bus_acquire(D1L_EXPANDER_I2C_BUS,
                                         SOLAR_OS_BUS_PROTOCOL_I2C,
                                         D1L_EXPANDER_BUS_OWNER);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "could not lease bus %s: %s",
                 D1L_EXPANDER_I2C_BUS, esp_err_to_name(err));
        return err;
    }

    err = tca9535_init(&expander, D1L_EXPANDER_I2C_BUS,
                       D1L_EXPANDER_ADDR_PRIMARY,
                       D1L_EXPANDER_ADDR_FALLBACK);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TCA9535 init failed: %s", esp_err_to_name(err));
        (void)solar_os_bus_release(D1L_EXPANDER_I2C_BUS,
                                   SOLAR_OS_BUS_PROTOCOL_I2C,
                                   D1L_EXPANDER_BUS_OWNER);
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
