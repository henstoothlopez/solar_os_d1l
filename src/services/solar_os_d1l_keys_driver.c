/* solar_os_d1l_keys_driver.c  (delivered 2026-10-23)
 *
 * Registration struct for the D1L RP2040 keyboard driver. Modelled on
 * solar_os_cardkb_driver.c, but with no bindings: the UART pins are fixed
 * by the board (GPIO19/GPIO20) and are set inside solar_os_d1l_keys.c. */

#include "solar_os_d1l_keys.h"

const solar_os_expansion_driver_t solar_os_d1l_keys_expansion_driver = {
    .name = "d1l-keys",
    .category = SOLAR_OS_EXPANSION_CATEGORY_INPUT,
    .summary = "CardKB keyboard via RP2040 UART bridge (D1L)",
    .required_capabilities = 0,
    .probe_supported = false,
    .binding_specs = NULL,
    .binding_spec_count = 0,
    .attach = solar_os_d1l_keys_attach,
    .detach = solar_os_d1l_keys_detach,
};
