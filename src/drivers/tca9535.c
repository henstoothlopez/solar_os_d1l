#include "tca9535.h"

#include <string.h>

#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "tca9535";

static esp_err_t write_reg(tca9535_t *dev, uint8_t reg, uint8_t value)
{
    return solar_os_bus_i2c_write_reg(dev->i2c_bus, dev->addr, reg, &value, 1);
}

static esp_err_t read_reg(tca9535_t *dev, uint8_t reg, uint8_t *value)
{
    return solar_os_bus_i2c_read_reg(dev->i2c_bus, dev->addr, reg, value, 1);
}

esp_err_t tca9535_init(tca9535_t *dev, const char *i2c_bus,
                       uint8_t addr_primary, uint8_t addr_fallback)
{
    if (dev == NULL || i2c_bus == NULL || i2c_bus[0] == '\0' || addr_primary == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(dev, 0, sizeof(*dev));
    strlcpy(dev->i2c_bus, i2c_bus, sizeof(dev->i2c_bus));

    if (solar_os_bus_i2c_probe(i2c_bus, addr_primary) == ESP_OK) {
        dev->addr = addr_primary;
    } else if (addr_fallback != 0 &&
               solar_os_bus_i2c_probe(i2c_bus, addr_fallback) == ESP_OK) {
        dev->addr = addr_fallback;
        ESP_LOGW(TAG, "TCA9535 found at fallback address 0x%02X", addr_fallback);
    } else {
        if (addr_fallback != 0) {
            ESP_LOGE(TAG, "TCA9535 not found at 0x%02X or 0x%02X",
                     addr_primary, addr_fallback);
        } else {
            ESP_LOGE(TAG, "TCA9535 not found at 0x%02X", addr_primary);
        }
        return ESP_ERR_NOT_FOUND;
    }

    /* Power-on default: every pin is INPUT on both ports. Read the real
     * config registers back rather than assuming, in case this expander
     * was already configured by something else before we got here. */
    esp_err_t err = read_reg(dev, TCA9535_REG_CONFIG0, &dev->config_shadow0);
    if (err != ESP_OK) return err;
    err = read_reg(dev, TCA9535_REG_CONFIG1, &dev->config_shadow1);
    if (err != ESP_OK) return err;
    err = read_reg(dev, TCA9535_REG_OUTPUT0, &dev->output_shadow0);
    if (err != ESP_OK) return err;
    err = read_reg(dev, TCA9535_REG_OUTPUT1, &dev->output_shadow1);
    if (err != ESP_OK) return err;

    dev->ready = true;
    return ESP_OK;
}

esp_err_t tca9535_set_pin_mode(tca9535_t *dev, uint8_t pin, bool is_output)
{
    if (dev == NULL || !dev->ready || pin > 15) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Explicit port dispatch — this is the exact bug class from the
     * Launcher port's expSetPin(): do not try to do this with a single
     * 16-bit shift into an 8-bit register. */
    if (pin < 8) {
        uint8_t bit = (uint8_t)(1u << pin);
        if (is_output) {
            dev->config_shadow0 &= (uint8_t)~bit; /* 0 = output */
        } else {
            dev->config_shadow0 |= bit;           /* 1 = input */
        }
        return write_reg(dev, TCA9535_REG_CONFIG0, dev->config_shadow0);
    } else {
        uint8_t bit = (uint8_t)(1u << (pin - 8));
        if (is_output) {
            dev->config_shadow1 &= (uint8_t)~bit;
        } else {
            dev->config_shadow1 |= bit;
        }
        return write_reg(dev, TCA9535_REG_CONFIG1, dev->config_shadow1);
    }
}

esp_err_t tca9535_write_pin(tca9535_t *dev, uint8_t pin, bool level)
{
    if (dev == NULL || !dev->ready || pin > 15) {
        return ESP_ERR_INVALID_ARG;
    }

    if (pin < 8) {
        uint8_t bit = (uint8_t)(1u << pin);
        if (level) {
            dev->output_shadow0 |= bit;
        } else {
            dev->output_shadow0 &= (uint8_t)~bit;
        }
        return write_reg(dev, TCA9535_REG_OUTPUT0, dev->output_shadow0);
    } else {
        uint8_t bit = (uint8_t)(1u << (pin - 8));
        if (level) {
            dev->output_shadow1 |= bit;
        } else {
            dev->output_shadow1 &= (uint8_t)~bit;
        }
        return write_reg(dev, TCA9535_REG_OUTPUT1, dev->output_shadow1);
    }
}

esp_err_t tca9535_read_pin(tca9535_t *dev, uint8_t pin, bool *level)
{
    if (dev == NULL || !dev->ready || pin > 15 || level == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t raw;
    esp_err_t err;
    if (pin < 8) {
        err = read_reg(dev, TCA9535_REG_INPUT0, &raw);
        if (err == ESP_OK) *level = (raw & (1u << pin)) != 0;
    } else {
        err = read_reg(dev, TCA9535_REG_INPUT1, &raw);
        if (err == ESP_OK) *level = (raw & (1u << (pin - 8))) != 0;
    }
    return err;
}

esp_err_t tca9535_pulse_low(tca9535_t *dev, uint8_t pin, uint32_t low_us)
{
    esp_err_t err = tca9535_set_pin_mode(dev, pin, true);
    if (err != ESP_OK) return err;
    err = tca9535_write_pin(dev, pin, false);
    if (err != ESP_OK) return err;
    esp_rom_delay_us(low_us);
    return tca9535_write_pin(dev, pin, true);
}
