/*
 * D1L-specific touch attach wrapper.
 *
 * WHY THIS FILE EXISTS: D1L's touch reset line is gated through the
 * TCA9535 expander (pin 7), not a raw ESP32 GPIO. The generic
 * src/drivers/ft6336.c and src/services/solar_os_ft6336.c already support
 * this (reset_pin=-1 means "caller already reset it" — see the
 * ft6336_2026-09-01.c patch), but something has to actually DO that pulse
 * before attach. Rather than hardcode D1L-specific TCA9535 calls into the
 * shared, board-agnostic solar_os_ft6336.c, this small wrapper owns that
 * one D1L-specific step and then forwards to the real, unmodified generic
 * attach function. Every other board keeps using the plain "ft6336" driver
 * untouched.
 *
 * Registered under a NEW driver name, "ft6336-d1l", so the D1L manifest's
 * touch0 device uses `driver = "ft6336-d1l"` instead of `driver = "ft6336"`.
 * Binding specs are copied verbatim from solar_os_ft6336_driver.c — this
 * wrapper passes bindings straight through unchanged to
 * solar_os_ft6336_attach(), it doesn't consume anything itself.
 */

#include "d1l_expander.h"
#include "solar_os_ft6336.h"

#define D1L_TOUCH_RST_PIN 7   /* TCA9535 pin — must match the board manifest's
                                 SOLAR_OS_BOARD_TCA9535_PIN_TOUCH_RST define */
#define D1L_TOUCH_RST_PULSE_US 5000  /* 5ms low pulse — matches the timing
                                         already proven on real D1L hardware
                                         during the Launcher port */

static const int addresses[] = {0x38, 0x48};
static const int fallback_addresses[] = {0x38, 0x48};
static const solar_os_expansion_binding_spec_t binding_specs[] = {
    {.key = "i2c", .value_hint = "bus", .kind = SOLAR_OS_EXPANSION_BINDING_I2C_BUS, .required = true},
    {.key = "addr", .value_hint = "0x48", .kind = SOLAR_OS_EXPANSION_BINDING_I2C_ADDRESS, .role = "addr", .required = true, .allowed_values = addresses, .allowed_value_count = sizeof(addresses) / sizeof(addresses[0])},
    {.key = "addr_fallback", .value_hint = "0x38", .kind = SOLAR_OS_EXPANSION_BINDING_I2C_ADDRESS, .role = "addr_fallback", .required = false, .allowed_values = fallback_addresses, .allowed_value_count = sizeof(fallback_addresses) / sizeof(fallback_addresses[0])},
    /* NOTE: "reset" intentionally NOT in this spec list — D1L's manifest
     * omits it, and this wrapper handles reset itself via the expander
     * before calling through. */
    /* CHANGED 2026-09-13: required=false — no confirmed touch IRQ pin
     * exists for D1L (checked against the Launcher port's notes, which
     * poll the FT6336U purely over I2C). D1L's manifest omits "irq"
     * entirely, same as it already omits "reset". See
     * solar_os_ft6336_driver_2026-09-13's comment for the full reasoning. */
    {.key = "irq", .value_hint = "gpio", .kind = SOLAR_OS_EXPANSION_BINDING_GPIO, .role = "irq", .required = false},
    {.key = "rotation", .value_hint = "0..3", .kind = SOLAR_OS_EXPANSION_BINDING_PARAMETER, .role = "rotation", .required = true, .has_value_range = true, .min_value = 0, .max_value = 3},
};

static esp_err_t attach_ft6336_d1l(const char *name,
                                   const solar_os_expansion_binding_t *bindings,
                                   size_t binding_count)
{
    esp_err_t err = d1l_expander_pulse(D1L_TOUCH_RST_PIN, D1L_TOUCH_RST_PULSE_US);
    if (err != ESP_OK) {
        return err; /* if the expander isn't reachable, touch definitely won't work either */
    }
    return solar_os_ft6336_attach(name, bindings, binding_count);
}

static esp_err_t detach_ft6336_d1l(const char *name)
{
    return solar_os_ft6336_detach(name);
}

const solar_os_expansion_driver_t solar_os_ft6336_d1l_expansion_driver = {
    .name = "ft6336-d1l",
    .summary = "capacitive touch (D1L, TCA9535-gated reset)",
    .required_capabilities = SOLAR_OS_BOARD_CAP_EXPANSION_I2C | SOLAR_OS_BOARD_CAP_EXPANSION_GPIO | SOLAR_OS_BOARD_CAP_GFX,
    .probe_supported = true,
    .binding_specs = binding_specs,
    .binding_spec_count = sizeof(binding_specs) / sizeof(binding_specs[0]),
    .attach = attach_ft6336_d1l,
    .detach = detach_ft6336_d1l,
};
