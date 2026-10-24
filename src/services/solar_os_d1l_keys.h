#pragma once

/* solar_os_d1l_keys.h  (delivered 2026-10-23)
 *
 * SenseCAP Indicator D1L keyboard driver. The CardKB2 plugged into the
 * Grove port is wired to the RP2040, not to the ESP32. This driver asks the
 * RP2040 for pending key bytes over UART1 (ESP32 GPIO19 = TX, GPIO20 = RX,
 * 115200 baud) using the same "keys" command the Launcher port uses.
 */

#include <stddef.h>

#include "esp_err.h"
#include "solar_os_expansion.h"

esp_err_t solar_os_d1l_keys_attach(const char *name,
                                   const solar_os_expansion_binding_t *bindings,
                                   size_t binding_count);
esp_err_t solar_os_d1l_keys_detach(const char *name);
