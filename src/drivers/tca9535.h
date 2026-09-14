#pragma once

/*
 * TCA9535 16-bit I2C IO expander driver.
 *
 * CORRECTED 2026-09-02: the original version of this file (dated
 * 2026-08-31) used raw ESP-IDF `i2c_master_bus_handle_t` calls
 * (i2c_master_bus_add_device/i2c_master_transmit). That's the wrong
 * abstraction for this codebase — the real ft6336.c you uploaded shows
 * every driver here talks I2C through solar_os_buses.h's name-based API
 * (solar_os_bus_i2c_read_reg(bus_name, addr, reg, ...) etc.), not raw
 * ESP-IDF handles. This version matches that pattern exactly. Found by
 * reading solar_os_buses.h directly (2026-09-02) rather than assuming the
 * first version's approach was fine because it compiled in isolation in
 * my head — it was never actually checked against a real header.
 *
 * KNOWN BUG CLASS AVOIDED HERE (found the hard way on the Launcher port for
 * this same board): a naive expSetPin() that does "1 << pin" on a single
 * uint8_t shadow byte silently overflows to 0 for any pin >= 8 (port1),
 * meaning port1 pins (e.g. RP2040 reset on IO8) get written nowhere and
 * never get configured as outputs (TCA9535 powers up with every pin as
 * INPUT). This driver dispatches to the correct port/register/shadow-byte
 * pair explicitly instead of doing bit math across a 16-bit pin space with
 * an 8-bit register underneath.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "solar_os_buses.h"

#define TCA9535_ADDR_PRIMARY 0x20
#define TCA9535_ADDR_FALLBACK 0x39

/* Register addresses (standard TCA9535 map). */
#define TCA9535_REG_INPUT0 0x00
#define TCA9535_REG_INPUT1 0x01
#define TCA9535_REG_OUTPUT0 0x02
#define TCA9535_REG_OUTPUT1 0x03
#define TCA9535_REG_POLARITY0 0x04
#define TCA9535_REG_POLARITY1 0x05
#define TCA9535_REG_CONFIG0 0x06
#define TCA9535_REG_CONFIG1 0x07

typedef struct {
    char i2c_bus[SOLAR_OS_BUS_NAME_MAX]; /* bus NAME, e.g. "i2c0" — not a handle */
    uint8_t addr;               /* resolved address after probe */
    uint8_t output_shadow0;     /* port0 (pins 0-7) output register shadow */
    uint8_t output_shadow1;     /* port1 (pins 8-15) output register shadow */
    uint8_t config_shadow0;     /* port0 direction shadow (1 = input) */
    uint8_t config_shadow1;     /* port1 direction shadow (1 = input) */
    bool ready;
} tca9535_t;

/*
 * Probes addr_primary, falls back to addr_fallback (0 = no fallback) on
 * no-ACK. i2c_bus is the bus NAME as declared in the board manifest's
 * [[buses]] table (e.g. "i2c0"), matching every other driver in this
 * codebase — not a raw handle.
 */
esp_err_t tca9535_init(tca9535_t *dev, const char *i2c_bus,
                       uint8_t addr_primary, uint8_t addr_fallback);

/* Configures a single pin (0-15) as output or input. Handles port0/port1
 * dispatch correctly — see the bug note above. */
esp_err_t tca9535_set_pin_mode(tca9535_t *dev, uint8_t pin, bool is_output);

/* Drives an output pin (0-15) high or low. Pin must already be configured
 * as output via tca9535_set_pin_mode(). */
esp_err_t tca9535_write_pin(tca9535_t *dev, uint8_t pin, bool level);

/* Reads an input pin (0-15). */
esp_err_t tca9535_read_pin(tca9535_t *dev, uint8_t pin, bool *level);

/* Convenience: configures `pin` as output (if not already), drives it low,
 * waits `low_us` microseconds, drives it high. Used for LCD RST / touch
 * RST pulse sequences. */
esp_err_t tca9535_pulse_low(tca9535_t *dev, uint8_t pin, uint32_t low_us);
