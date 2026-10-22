/*
 * solar_os_ft6336_driver.c  --  version 2026-10-21
 *
 * ONLY CODE CHANGE FROM PREVIOUS VERSION: the "addr" binding spec no longer
 * has .role = "addr". The manifest generator gives I2C-address bindings an
 * empty role, and the shared checker compares roles exactly, so a named spec
 * rejects every generated address binding. A spec without a role matches any
 * I2C-address binding (this is how upstream had it).
 * The long explanatory comments from the earlier version were shortened;
 * behavior is otherwise identical.
 */

#include "solar_os_ft6336.h"

/* 0x48 is the FT6336U address on the SenseCAP Indicator D1L (GX variant);
 * 0x38 is the address used by other boards. */
static const int addresses[] = {0x38, 0x48};
static const int fallback_addresses[] = {0x38, 0x48};
static const solar_os_expansion_binding_spec_t binding_specs[] = {
    {.key = "i2c", .value_hint = "bus", .kind = SOLAR_OS_EXPANSION_BINDING_I2C_BUS, .required = true},
    {.key = "addr", .value_hint = "0x38", .kind = SOLAR_OS_EXPANSION_BINDING_I2C_ADDRESS, .required = true, .allowed_values = addresses, .allowed_value_count = sizeof(addresses) / sizeof(addresses[0])},
    /* Optional second address, for hardware revisions that answer elsewhere. */
    {.key = "addr_fallback", .value_hint = "0x48", .kind = SOLAR_OS_EXPANSION_BINDING_I2C_ADDRESS, .role = "addr_fallback", .required = false, .allowed_values = fallback_addresses, .allowed_value_count = sizeof(fallback_addresses) / sizeof(fallback_addresses[0])},
    /* reset and irq are optional: boards that gate them through an IO
     * expander, or have no interrupt line, simply omit them. */
    {.key = "reset", .value_hint = "gpio", .kind = SOLAR_OS_EXPANSION_BINDING_GPIO, .role = "reset", .required = false},
    {.key = "irq", .value_hint = "gpio", .kind = SOLAR_OS_EXPANSION_BINDING_GPIO, .role = "irq", .required = false},
    {.key = "rotation", .value_hint = "0..3", .kind = SOLAR_OS_EXPANSION_BINDING_PARAMETER, .role = "rotation", .required = true, .has_value_range = true, .min_value = 0, .max_value = 3},
};

const solar_os_expansion_driver_t solar_os_ft6336_expansion_driver = {
    .name = "ft6336",
    .category = SOLAR_OS_EXPANSION_CATEGORY_INPUT,
    .summary = "capacitive touch",
    .required_capabilities = SOLAR_OS_BOARD_CAP_EXPANSION_I2C | SOLAR_OS_BOARD_CAP_EXPANSION_GPIO | SOLAR_OS_BOARD_CAP_GFX,
    .probe_supported = true,
    .binding_specs = binding_specs,
    .binding_spec_count = sizeof(binding_specs) / sizeof(binding_specs[0]),
    .attach = solar_os_ft6336_attach,
    .detach = solar_os_ft6336_detach,
};
