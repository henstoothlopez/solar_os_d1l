#include "solar_os_ft6336.h"

static const int addresses[] = {0x38, 0x48}; /* CORRECTED 2026-09-01: 0x48 added
    as an allowed PRIMARY address, not just a fallback. The real D1L board
    .ini (platformio_SenseCAP-D1L.ini) shows 0x48 is FT6336U's actual
    address on this hardware (labeled "GX variant, ST7701S+FT6336U"), while
    0x38 belongs to a DIFFERENT chip (FT5x06) on a DIFFERENT board variant
    (DX, different LCD controller entirely) — not a same-chip fallback
    address as earlier notes assumed. D1L's manifest sets addr=0x48,
    addr_fallback=0x38 (fallback kept only as a low-confidence probe; if it
    responds, it likely means a DX-variant board with different touch/
    display hardware is actually present, not the same chip at another
    address). 0x38 stays in the primary list too for whatever board
    originally relied on it as primary — this change is additive. */
static const int fallback_addresses[] = {0x38, 0x48}; /* CORRECTED 2026-09-01:
    either value may legitimately be used as the *other* one's fallback
    depending on which is primary for a given board — see above. */
static const solar_os_expansion_binding_spec_t binding_specs[] = {
    {.key = "i2c", .value_hint = "bus", .kind = SOLAR_OS_EXPANSION_BINDING_I2C_BUS, .required = true},
    {.key = "addr", .value_hint = "0x38", .kind = SOLAR_OS_EXPANSION_BINDING_I2C_ADDRESS, .role = "addr", .required = true, .allowed_values = addresses, .allowed_value_count = sizeof(addresses) / sizeof(addresses[0])},
    /* NEW: optional second address for boards where the FT6336U answers at
     * an alternate address depending on hardware revision (confirmed on
     * the SenseCAP Indicator D1L — see SolarOS-D1L-Port-Roadmap.md
     * section 5). Boards that don't bind this key get address_fallback=0
     * in solar_os_ft6336.c, meaning "no fallback", identical to today's
     * behavior. */
    {.key = "addr_fallback", .value_hint = "0x48", .kind = SOLAR_OS_EXPANSION_BINDING_I2C_ADDRESS, .role = "addr_fallback", .required = false, .allowed_values = fallback_addresses, .allowed_value_count = sizeof(fallback_addresses) / sizeof(fallback_addresses[0])},
    /* CHANGED: required=false (was true). Boards whose touch reset line is
     * gated through an IO expander instead of a raw ESP32 GPIO (e.g.
     * D1L's TCA9535) omit this binding and pulse the reset themselves
     * before the display/touch attach sequence runs. Boards that do bind
     * "reset" keep exactly the same behavior as before. */
    {.key = "reset", .value_hint = "gpio", .kind = SOLAR_OS_EXPANSION_BINDING_GPIO, .role = "reset", .required = false},
    /* CHANGED 2026-09-13: required=false (was true). Confirmed via the
     * Launcher port's proven, working touch implementation on this same
     * D1L hardware (pure I2C register polling, no interrupt line ever
     * identified in any hardware note) AND by reading ft6336.c itself
     * (irq_pin is configured as a GPIO input but never actually read
     * anywhere — functionally vestigial in this driver). Boards that omit
     * "irq" lose nothing; boards that do bind it keep existing behavior. */
    {.key = "irq", .value_hint = "gpio", .kind = SOLAR_OS_EXPANSION_BINDING_GPIO, .role = "irq", .required = false},
    {.key = "rotation", .value_hint = "0..3", .kind = SOLAR_OS_EXPANSION_BINDING_PARAMETER, .role = "rotation", .required = true, .has_value_range = true, .min_value = 0, .max_value = 3},
};

const solar_os_expansion_driver_t solar_os_ft6336_expansion_driver = {
    .name = "ft6336",
    .summary = "capacitive touch",
    .required_capabilities = SOLAR_OS_BOARD_CAP_EXPANSION_I2C | SOLAR_OS_BOARD_CAP_EXPANSION_GPIO | SOLAR_OS_BOARD_CAP_GFX,
    .probe_supported = true,
    .binding_specs = binding_specs,
    .binding_spec_count = sizeof(binding_specs) / sizeof(binding_specs[0]),
    .attach = solar_os_ft6336_attach,
    .detach = solar_os_ft6336_detach,
};
