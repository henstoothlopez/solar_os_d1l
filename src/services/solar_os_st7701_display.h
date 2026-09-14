#pragma once

/*
 * Glue layer implementing solar_os_board_display_ops_t for the D1L's
 * ST7701S RGB-parallel panel, following the exact pattern of
 * solar_os_tft_display.c (the real ILI9341/ST7796 glue) — same ops table
 * shape, same u8g2 "user-defined display" wiring approach (u8g2's own
 * monochrome tile buffer drives simple text/terminal drawing via
 * DRAW_TILE; the richer INDEX8 surface/raster compositor path is separate
 * and handled directly here via present_surface/present_frame).
 *
 * STATUS 2026-09-03: written against real, verified facts (the actual
 * solar_os_board_display_ops_t contract, the actual ili9341 u8x8 callback
 * pattern, the actual name-based I2C bus API, and now the actual
 * hardware-verified ST7701S init sequence from D1L_ST7701_Init.cpp) — NOT
 * hardware-tested as a whole. Concrete gaps remain, each flagged at its
 * exact location in the .c file:
 *   1. run_preinit_sequence() is a byte-for-byte port of
 *      D1L_ST7701_Init.cpp, but porting Arduino digitalWrite() to
 *      ESP-IDF gpio_set_level() may have lost timing margin the bit-banged
 *      protocol depended on — see the function's own comment for what to
 *      try if the panel doesn't respond. Also: CS toggles hundreds of
 *      times through the TCA9535 over I2C during this sequence, so expect
 *      it to take a real, noticeable amount of time — not a bug.
 *   2. Several esp_lcd_rgb_panel_config_t fields in rgb_st7701.c are
 *      still marked TUNE ON HARDWARE (pclk_hz, bounce buffer size, PCLK edge).
 *   3. present_surface() intentionally skips the tile-hash dirty-diffing
 *      optimization real SPI drivers use (see the .c file for why that's
 *      a defensible simplification for a continuously-scanned panel, not
 *      a shortcut) — worth a perf look once something is on screen at all.
 */

#include <stddef.h>

#include "esp_err.h"
#include "solar_os_expansion.h"

esp_err_t solar_os_st7701_attach(const char *name,
                                 const solar_os_expansion_binding_t *bindings,
                                 size_t binding_count);
esp_err_t solar_os_st7701_detach(const char *name);
