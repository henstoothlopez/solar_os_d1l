#include "rgb_st7701.h"

#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "rgb_st7701";

struct rgb_st7701_s {
    esp_lcd_panel_handle_t panel;
    rgb_st7701_config_t cfg;
    bool backlight_pwm_ready;
};

static esp_err_t init_backlight(rgb_st7701_t *dev)
{
    if (dev->cfg.backlight_gpio < 0) {
        return ESP_OK;
    }
    if (!dev->cfg.backlight_pwm) {
        /* plain on/off GPIO, no PWM channel needed */
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << dev->cfg.backlight_gpio,
            .mode = GPIO_MODE_OUTPUT,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "backlight gpio_config");
        gpio_set_level(dev->cfg.backlight_gpio,
                       dev->cfg.backlight_active_high ? 1 : 0);
        return ESP_OK;
    }

    /* TUNE ON HARDWARE: timer/channel numbers below assume nothing else on
     * this board claims LEDC_TIMER_0 / LEDC_CHANNEL_0. Confirm against the
     * rest of the board init before first flash. */
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 20000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_cfg), TAG, "ledc_timer_config");

    ledc_channel_config_t ch_cfg = {
        .gpio_num = dev->cfg.backlight_gpio,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&ch_cfg), TAG, "ledc_channel_config");
    dev->backlight_pwm_ready = true;
    return ESP_OK;
}

esp_err_t rgb_st7701_set_backlight(rgb_st7701_t *dev, uint8_t percent)
{
    if (dev == NULL || dev->cfg.backlight_gpio < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (percent > 100) percent = 100;

    if (!dev->cfg.backlight_pwm) {
        bool on = percent > 0;
        gpio_set_level(dev->cfg.backlight_gpio,
                        dev->cfg.backlight_active_high ? on : !on);
        return ESP_OK;
    }
    if (!dev->backlight_pwm_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    uint32_t duty = (percent * 255U) / 100U;
    if (!dev->cfg.backlight_active_high) {
        duty = 255U - duty;
    }
    ESP_RETURN_ON_ERROR(
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty), TAG, "set_duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

esp_err_t rgb_st7701_init(const rgb_st7701_config_t *config, rgb_st7701_t **out_dev)
{
    if (config == NULL || out_dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    rgb_st7701_t *dev = calloc(1, sizeof(rgb_st7701_t));
    if (dev == NULL) {
        return ESP_ERR_NO_MEM;
    }
    dev->cfg = *config;

    esp_err_t err = init_backlight(dev);
    if (err != ESP_OK) {
        free(dev);
        return err;
    }

    /*
     * RESOLVED 2026-09-03: config->run_preinit_sequence() is now a real
     * implementation, not a placeholder — see
     * solar_os_st7701_display.c's run_preinit_sequence(), ported
     * verbatim (byte-for-byte) from the real, hardware-verified
     * D1L_ST7701_Init.cpp. Kept as a function pointer here rather than
     * hardcoding the sequence into this file, since this file (rgb_st7701.c)
     * is meant to stay generic RGB-panel/esp_lcd_rgb_panel plumbing, while
     * the actual bit-banged command bytes are D1L-specific and belong in
     * the D1L glue layer instead. Ordering matters and is preserved: this
     * runs BEFORE esp_lcd_new_rgb_panel()/esp_lcd_panel_init() below,
     * matching the proven ordering from the Launcher port's
     * _setup_gpio() running before tft->begin().
     */
    if (config->run_preinit_sequence != NULL) {
        err = config->run_preinit_sequence();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ST7701S pre-init sequence failed: %s", esp_err_to_name(err));
            free(dev);
            return err;
        }
    } else {
        ESP_LOGW(TAG, "No run_preinit_sequence provided — panel will very "
                       "likely stay blank. This is expected until the "
                       "Launcher port's init sequence is ported in.");
    }

    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            /* TUNE ON HARDWARE: pclk_hz was never independently derived
             * for this panel on the Launcher port either — Arduino_GFX
             * picked a default. Start conservative and raise once a
             * stable image is confirmed. */
            .pclk_hz = 16 * 1000 * 1000,
            .h_res = config->width,
            .v_res = config->height,
            .hsync_pulse_width = config->hsync_pulse_width,
            .hsync_back_porch = config->hsync_back_porch,
            .hsync_front_porch = config->hsync_front_porch,
            .vsync_pulse_width = config->vsync_pulse_width,
            .vsync_back_porch = config->vsync_back_porch,
            .vsync_front_porch = config->vsync_front_porch,
            .flags = {
                .pclk_active_neg = true, /* TUNE ON HARDWARE */
            },
        },
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = 1, /* TUNE ON HARDWARE: 2 for double-buffering if tearing shows up */
        .bounce_buffer_size_px = 4800, /* CHANGED 2026-10-24: 10 rows of 480 px; fixes right-drift on redraw. Original note follows: */ /* TUNE ON HARDWARE: nonzero bounce buffer
                                        may be needed depending on PSRAM
                                        bandwidth headroom at this
                                        resolution/refresh rate */
        .sram_trans_align = 4,
        .psram_trans_align = 64,
        .hsync_gpio_num = config->hsync_gpio,
        .vsync_gpio_num = config->vsync_gpio,
        .de_gpio_num = config->de_gpio,
        .pclk_gpio_num = config->pclk_gpio,
        .disp_gpio_num = -1,
        .flags = {
            .fb_in_psram = true,
        },
    };
    memcpy(panel_config.data_gpio_nums, config->data_gpio,
           sizeof(panel_config.data_gpio_nums));

    err = esp_lcd_new_rgb_panel(&panel_config, &dev->panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_rgb_panel failed: %s", esp_err_to_name(err));
        free(dev);
        return err;
    }
    ESP_LOGI(TAG, "esp_lcd_new_rgb_panel() succeeded");

    /* FIXED 2026-09-17 (code-review item 6): esp_lcd_panel_reset()/_init()
     * used to run through ESP_RETURN_ON_ERROR(), which returns immediately
     * on failure WITHOUT deleting the just-created panel handle or freeing
     * `dev` — a real leak on any retry/re-attach path after a failed init.
     * Now handled explicitly so every failure path cleans up. */
    err = esp_lcd_panel_reset(dev->panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel_reset failed: %s", esp_err_to_name(err));
        esp_lcd_panel_del(dev->panel);
        free(dev);
        return err;
    }

    err = esp_lcd_panel_init(dev->panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel_init failed: %s", esp_err_to_name(err));
        esp_lcd_panel_del(dev->panel);
        free(dev);
        return err;
    }
    ESP_LOGI(TAG, "panel initialization succeeded");

    /* FIXED 2026-09-17 (code-review item 1, HIGH): the LEDC channel used
     * to be configured with .duty = 0 and nothing afterward ever reliably
     * turned it on — the only intended path was u8g2 issuing
     * U8X8_MSG_DISPLAY_SET_POWER_SAVE, which never fires if u8g2 believes
     * power-save is already disabled. That meant the panel could be fully
     * initialized and scanning out real pixel data while the backlight
     * stayed dark the entire time, indistinguishable from a real
     * init failure. Force the backlight on here, right after a
     * successful panel_init(), so backlight state no longer depends on
     * u8g2's internal assumptions at all. */
    err = rgb_st7701_set_backlight(dev, 100);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "backlight enable failed: %s", esp_err_to_name(err));
        esp_lcd_panel_del(dev->panel);
        free(dev);
        return err;
    }
    ESP_LOGI(TAG, "backlight was set to 100%%");

    *out_dev = dev;
    return ESP_OK;
}

esp_err_t rgb_st7701_present_rect(rgb_st7701_t *dev, uint16_t x, uint16_t y,
                                  uint16_t width, uint16_t height,
                                  const uint16_t *rgb565_data)
{
    if (dev == NULL || rgb565_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((uint32_t)x + width > dev->cfg.width || (uint32_t)y + height > dev->cfg.height) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_lcd_panel_draw_bitmap(dev->panel, x, y,
                                     (int)x + width, (int)y + height,
                                     rgb565_data);
}

esp_err_t rgb_st7701_present(rgb_st7701_t *dev, const uint16_t *rgb565_data)
{
    if (dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return rgb_st7701_present_rect(dev, 0, 0, dev->cfg.width, dev->cfg.height,
                                   rgb565_data);
}

esp_err_t rgb_st7701_deinit(rgb_st7701_t *dev)
{
    if (dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (dev->panel != NULL) {
        esp_lcd_panel_del(dev->panel);
    }
    free(dev);
    return ESP_OK;
}
