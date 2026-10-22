/*
 * solar_os_ft6336_d1l.c  --  version 2026-10-21
 *
 * D1L-specific touch attach wrapper. Its job: pulse the touch reset line
 * (wired through the TCA9535 expander, pin 7) and then hand off to the
 * generic solar_os_ft6336_attach().
 *
 * CHANGES FROM PREVIOUS VERSION (2026-10-21):
 *  1. The "addr" binding spec NO LONGER has .role = "addr". The manifest
 *     generator gives I2C-address bindings an EMPTY role, and the shared
 *     checker in solar_os_expansion.c compares roles exactly, so the old
 *     spec rejected every generated address binding ("invalid bindings").
 *     A spec with no role matches any I2C address binding, as upstream does.
 *  2. The fallback address is now a PARAMETER binding named "addr_fallback"
 *     (the generator DOES emit role names for parameters, like "rotation").
 *  3. This wrapper chooses the working address itself: it probes the primary
 *     address, and if nothing answers it probes the fallback, then forwards
 *     only i2c + addr + rotation to the shared touch code. The shared
 *     solar_os_ft6336.c is therefore not touched by this change.
 *  4. Added .category = INPUT so "expansion drivers" lists it under Input.
 *  5. Added a 200 ms wait after releasing touch reset (the FT6336 needs time
 *     before it answers on I2C).
 */

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "d1l_expander.h"
#include "solar_os_buses.h"
#include "solar_os_ft6336.h"

#define D1L_TOUCH_RST_PIN 7          /* TCA9535 pin */
#define D1L_TOUCH_RST_PULSE_US 5000  /* 5 ms low pulse */
#define D1L_TOUCH_RESET_SETTLE_MS 200

static const char *TAG = "ft6336_d1l";

static const int addresses[] = {0x38, 0x48};
static const int fallback_addresses[] = {0x38, 0x48};
static const solar_os_expansion_binding_spec_t binding_specs[] = {
    {.key = "i2c", .value_hint = "bus", .kind = SOLAR_OS_EXPANSION_BINDING_I2C_BUS, .required = true},
    /* No .role on purpose -- see change 1 above. */
    {.key = "addr", .value_hint = "0x48", .kind = SOLAR_OS_EXPANSION_BINDING_I2C_ADDRESS, .required = true, .allowed_values = addresses, .allowed_value_count = sizeof(addresses) / sizeof(addresses[0])},
    {.key = "addr_fallback", .value_hint = "0x38", .kind = SOLAR_OS_EXPANSION_BINDING_PARAMETER, .role = "addr_fallback", .required = false, .allowed_values = fallback_addresses, .allowed_value_count = sizeof(fallback_addresses) / sizeof(fallback_addresses[0])},
    {.key = "irq", .value_hint = "gpio", .kind = SOLAR_OS_EXPANSION_BINDING_GPIO, .role = "irq", .required = false},
    {.key = "rotation", .value_hint = "0..3", .kind = SOLAR_OS_EXPANSION_BINDING_PARAMETER, .role = "rotation", .required = true, .has_value_range = true, .min_value = 0, .max_value = 3},
};

static esp_err_t attach_ft6336_d1l(const char *name,
                                   const solar_os_expansion_binding_t *bindings,
                                   size_t binding_count)
{
    if (bindings == NULL || binding_count == 0 ||
        binding_count > SOLAR_OS_EXPANSION_DEVICE_BINDING_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = d1l_expander_pulse(D1L_TOUCH_RST_PIN, D1L_TOUCH_RST_PULSE_US);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "touch reset via expander failed: %s", esp_err_to_name(err));
        return err; /* if the expander is unreachable, touch will not work either */
    }
    vTaskDelay(pdMS_TO_TICKS(D1L_TOUCH_RESET_SETTLE_MS));

    /* Copy the bindings, dropping "addr_fallback" (handled here, not by the
     * shared code), and remember where the bus name and address are. */
    solar_os_expansion_binding_t forwarded[SOLAR_OS_EXPANSION_DEVICE_BINDING_MAX];
    size_t forwarded_count = 0;
    int addr_index = -1;
    int fallback_addr = 0;
    const char *i2c_bus = NULL;

    for (size_t i = 0; i < binding_count; i++) {
        const solar_os_expansion_binding_t *b = &bindings[i];
        if (b->kind == SOLAR_OS_EXPANSION_BINDING_PARAMETER &&
            strcmp(b->role, "addr_fallback") == 0) {
            fallback_addr = b->value;
            continue;
        }
        forwarded[forwarded_count] = *b;
        if (b->kind == SOLAR_OS_EXPANSION_BINDING_I2C_BUS) {
            i2c_bus = b->target;
        } else if (b->kind == SOLAR_OS_EXPANSION_BINDING_I2C_ADDRESS) {
            addr_index = (int)forwarded_count;
        }
        forwarded_count++;
    }

    if (i2c_bus != NULL && addr_index >= 0) {
        const int primary = forwarded[addr_index].value;
        if (solar_os_bus_i2c_probe(i2c_bus, (uint8_t)primary) == ESP_OK) {
            ESP_LOGI(TAG, "touch answered at 0x%02X", primary);
        } else if (fallback_addr != 0 &&
                   solar_os_bus_i2c_probe(i2c_bus, (uint8_t)fallback_addr) == ESP_OK) {
            ESP_LOGW(TAG, "touch not at 0x%02X, using fallback 0x%02X",
                     primary, fallback_addr);
            forwarded[addr_index].value = fallback_addr;
        } else {
            ESP_LOGE(TAG, "touch did not answer at 0x%02X%s",
                     primary, fallback_addr != 0 ? " or its fallback" : "");
            /* Still forward: the shared code reports its own error. */
        }
    }

    return solar_os_ft6336_attach(name, forwarded, forwarded_count);
}

static esp_err_t detach_ft6336_d1l(const char *name)
{
    return solar_os_ft6336_detach(name);
}

const solar_os_expansion_driver_t solar_os_ft6336_d1l_expansion_driver = {
    .name = "ft6336-d1l",
    .category = SOLAR_OS_EXPANSION_CATEGORY_INPUT,
    .summary = "capacitive touch (D1L, TCA9535-gated reset)",
    .required_capabilities = SOLAR_OS_BOARD_CAP_EXPANSION_I2C | SOLAR_OS_BOARD_CAP_EXPANSION_GPIO | SOLAR_OS_BOARD_CAP_GFX,
    .probe_supported = true,
    .binding_specs = binding_specs,
    .binding_spec_count = sizeof(binding_specs) / sizeof(binding_specs[0]),
    .attach = attach_ft6336_d1l,
    .detach = detach_ft6336_d1l,
};
