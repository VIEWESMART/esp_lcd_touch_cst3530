/*
 * SPDX-FileCopyrightText: 2026 Shenzhen Viewe Technology Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_lcd_touch_cst3530.h
 * @brief ESP LCD touch driver for CST3530 (HYN cst66xx series)
 *
 * @author Ayang
 * @copyright Copyright (c) 2026 Shenzhen Viewe Technology Co., Ltd.
 */

#pragma once

#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a new CST3530 touch driver
 *
 * @note  I2C bus must be initialized via `i2c_new_master_bus()` and panel IO via
 *        `esp_lcd_new_panel_io_i2c()`. Set `ESP_LCD_TOUCH_IO_I2C_CST3530_CONFIG().user_ctx`
 *        to the `i2c_master_bus_handle_t` before creating panel IO.
 *
 * @param io     LCD panel IO handle from `esp_lcd_new_panel_io_i2c()`
 * @param config Touch panel configuration
 * @param tp     Touch panel handle
 * @return
 *      - ESP_OK on success
 */
esp_err_t esp_lcd_touch_new_i2c_cst3530(const esp_lcd_panel_io_handle_t io,
                                        const esp_lcd_touch_config_t *config,
                                        esp_lcd_touch_handle_t *tp);

/** @brief Main mode I2C address of CST3530 */
#define ESP_LCD_TOUCH_IO_I2C_CST3530_ADDRESS    (0x58)

/**
 * @brief Touch IO configuration structure
 *
 * @note Assign `user_ctx` to your `i2c_master_bus_handle_t` after expanding the macro.
 */
#define ESP_LCD_TOUCH_IO_I2C_CST3530_CONFIG()              \
    {                                                      \
        .scl_speed_hz = 400000,                            \
        .dev_addr = ESP_LCD_TOUCH_IO_I2C_CST3530_ADDRESS,  \
        .on_color_trans_done = 0,                          \
        .user_ctx = 0,                                     \
        .control_phase_bytes = 1,                          \
        .dc_bit_offset = 0,                                \
        .lcd_cmd_bits = 8,                                 \
        .lcd_param_bits = 0,                               \
        .flags = {                                         \
            .dc_low_on_data = 0,                           \
            .disable_control_phase = 1,                    \
        },                                                 \
    }

#ifdef __cplusplus
}
#endif
