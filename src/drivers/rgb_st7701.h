#pragma once

/*
 * ST7701S RGB-parallel (DPI) display driver for the SenseCAP Indicator D1L.
 *
 * STATUS 2026-09-03: NOT HARDWARE-TESTED, but no longer a skeleton for the
 * init sequence — solar_os_st7701_display.c's run_preinit_sequence() is a
 * real, byte-for-byte port of the hardware-verified D1L_ST7701_Init.cpp
 * (see that file's own header comment for the two real porting risks:
 * gpio_set_level() has less implicit timing slack than Arduino's
 * digitalWrite() did, and CS toggles hundreds of times through the
 * TCA9535 over I2C, which will be slow but should still be correct).
 *
 * What IS proven and carried over as-is from the Launcher port on this
 * same board:
 *   - the bit-banged 9-bit-SPI pre-init command sequence — now actually
 *     ported (see above), not just referenced
 *   - timing porches (see the manifest's SOLAR_OS_BOARD_LCD_*_PORCH defines)
 *   - rotation=2, 480x480, RGB565
 *
 * What is NOT yet verified and is marked TUNE ON HARDWARE below — this
 * list is now SHORTER than earlier drafts, the init sequence is no
 * longer on it:
 *   - bounce buffer size / whether a bounce buffer is needed at all with a
 *     full frame buffer in PSRAM
 *   - PCLK active edge polarity
 *   - DMA transfer alignment
 *   - exact field names/shape of esp_lcd_rgb_panel_config_t in whatever
 *     ESP-IDF version pioarduino has pinned for this SolarOS checkout —
 *     verify against the actual installed
 *     esp_lcd_rgb_panel.h/esp_lcd_types.h before first build, the API has
 *     changed shape across IDF versions before.
 *   - pclk_hz itself — never independently derived even on the Launcher
 *     port, which used an Arduino_GFX default. Start conservative (see
 *     rgb_st7701.c) and raise once a stable image is confirmed.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_types.h"

typedef struct rgb_st7701_s rgb_st7701_t;

typedef struct {
    int hsync_gpio;
    int vsync_gpio;
    int de_gpio;
    int pclk_gpio;
    int data_gpio[16];   /* R0-4, G0-5, B0-4 in that order, 16 total */
    int backlight_gpio;  /* -1 if none */
    bool backlight_active_high;
    bool backlight_pwm;

    uint16_t width;
    uint16_t height;

    uint16_t hsync_pulse_width;
    uint16_t hsync_back_porch;
    uint16_t hsync_front_porch;
    uint16_t vsync_pulse_width;
    uint16_t vsync_back_porch;
    uint16_t vsync_front_porch;

    /* Called before esp_lcd_panel_init() to run the bit-banged ST7701S
     * command sequence. Implemented in solar_os_st7701_display.c's
     * run_preinit_sequence() — a byte-for-byte port of the real,
     * hardware-verified D1L_ST7701_Init.cpp. Wired up at the call site
     * in solar_os_st7701_attach(). */
    esp_err_t (*run_preinit_sequence)(void);
} rgb_st7701_config_t;

esp_err_t rgb_st7701_init(const rgb_st7701_config_t *config, rgb_st7701_t **out_dev);
esp_err_t rgb_st7701_deinit(rgb_st7701_t *dev);

/* Full-frame present: draws `data` (RGB565, width*height*2 bytes) into the
 * panel's DMA-backed frame buffer. For a continuously-scanned-out DPI
 * panel this just needs to update the buffer contents — the peripheral
 * keeps refreshing the physical panel from it automatically, same
 * conceptual model as vga32.c's scanout buffer. */
esp_err_t rgb_st7701_present(rgb_st7701_t *dev, const uint16_t *rgb565_data);

/* ADDED 2026-09-02: sub-rectangle present, needed by
 * solar_os_st7701_display.c's present_surface()/present_frame() so a
 * small dirty region doesn't force converting and pushing the full
 * 480x480 frame every time. `rgb565_data` must be exactly
 * width*height pixels, tightly packed (no stride padding) — the caller
 * (the glue layer) is responsible for producing a tightly-packed buffer
 * for the sub-rectangle before calling this. */
esp_err_t rgb_st7701_present_rect(rgb_st7701_t *dev, uint16_t x, uint16_t y,
                                  uint16_t width, uint16_t height,
                                  const uint16_t *rgb565_data);

esp_err_t rgb_st7701_set_backlight(rgb_st7701_t *dev, uint8_t percent);
