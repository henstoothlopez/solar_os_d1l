#pragma once

/*
 * D1L-specific TCA9535 expander access, shared by the display and touch
 * attach paths.
 *
 * WHY THIS FILE EXISTS: SolarOS's expansion framework (solar_os_expansion.c)
 * has no public API for one already-attached device's driver to fetch
 * another already-attached device's live instance by name — checked
 * directly: solar_os_expansion_device_t (the type returned by
 * solar_os_expansion_get_device()) stores only name/driver/bindings
 * metadata, no opaque per-driver context pointer. Rather than extend that
 * shared framework file (used by every board) for a need specific to one
 * board's hardware quirk, this gives D1L's own display and touch attach
 * paths a small, self-contained way to talk to the TCA9535 without
 * depending on each other or on framework changes.
 *
 * Lazily initializes on first use and is safe to call from both the
 * display attach path (early pass) and the touch attach path (normal
 * pass, runs later) — TCA9535 register writes are idempotent and cheap;
 * there's no meaningful cost to it being "already initialized" by the
 * time touch calls it.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/* Pulses `pin` (0-15) low for `low_us` microseconds then high, configuring
 * it as an output first if needed. Lazily initializes the shared TCA9535
 * handle (bus "i2c0", address 0x20 primary / 0x39 fallback — matching the
 * board manifest's expander0 bindings) on first call. */
esp_err_t d1l_expander_pulse(uint8_t pin, uint32_t low_us);

/* Drives `pin` to a fixed level without pulsing (e.g. holding LoRa CS/RST
 * deasserted) — configures as output first if needed. */
esp_err_t d1l_expander_write(uint8_t pin, bool level);

/* ADDED 2026-09-03: identical to d1l_expander_write() but skips the
 * tca9535_set_pin_mode() call every time. Needed for the ST7701S
 * bit-banged init sequence, which toggles the LCD_CS pin (via the
 * expander, over I2C) hundreds of times — d1l_expander_write()
 * reconfiguring pin direction on every single toggle would roughly
 * double the already-slow I2C traffic for no reason once the pin's
 * already been configured as output once. Call d1l_expander_write() ONCE
 * first to configure the pin as output, then use this for the repeated
 * toggles that follow. Using this before the pin has ever been
 * configured as output is a bug (undefined pin direction) — it doesn't
 * self-configure. */
esp_err_t d1l_expander_write_fast(uint8_t pin, bool level);
