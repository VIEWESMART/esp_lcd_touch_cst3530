/*
 * Layout compatible with esp_lcd_panel_io_i2c_v2.c (ESP-IDF 5.x i2c_master bus).
 * Used to obtain the I2C device handle and user_ctx (bus handle) from panel_io.
 */
#pragma once

#include "esp_lcd_panel_io_interface.h"
#include "driver/i2c_master.h"
#include <sys/cdefs.h>

typedef struct {
    esp_lcd_panel_io_t base;
    i2c_master_dev_handle_t i2c_handle;
    uint32_t dev_addr;
    int lcd_cmd_bits;
    int lcd_param_bits;
    bool control_phase_enabled;
    uint32_t control_phase_cmd;
    uint32_t control_phase_data;
    void *on_color_trans_done;
    void *user_ctx;
} hyn_lcd_panel_io_i2c_t;

static inline hyn_lcd_panel_io_i2c_t *hyn_lcd_panel_io_i2c_from_handle(esp_lcd_panel_io_handle_t io)
{
    return __containerof(io, hyn_lcd_panel_io_i2c_t, base);
}
