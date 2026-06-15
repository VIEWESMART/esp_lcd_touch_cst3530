/*
 * SPDX-FileCopyrightText: 2026 Shenzhen Viewe Technology Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_cst3530.h"
#include "hyn_core.h"

static const char *TAG = "CST3530";

/* Release if chip stops reporting while finger was down (missed lift packet) */
#define CST3530_TOUCH_RELEASE_TIMEOUT_MS  80

typedef struct {
    struct hyn_ts_data ts_data;
    uint16_t chip_x_res;
    uint16_t chip_y_res;
    bool map_swap_xy;
    bool map_mirror_x;
    bool map_mirror_y;
    bool touch_active;
    uint32_t last_active_ms;
    uint16_t last_x;
    uint16_t last_y;
    uint16_t last_strength;
    uint8_t last_track_id;
} cst3530_priv_t;

static void cst3530_map_to_panel(esp_lcd_touch_handle_t tp, uint16_t raw_x, uint16_t raw_y,
                                 uint16_t *panel_x, uint16_t *panel_y)
{
    cst3530_priv_t *priv = tp->config.driver_data;
    /*
     * CO5300: fw_res_x=1210 (RX/vertical), fw_res_y=568 (TX/horizontal).
     * pos_x is in ~0..568, pos_y is in ~0..1210 — use matching norm per axis.
     */
    uint16_t chip_x_norm = priv->chip_x_res ? priv->chip_x_res : 1210;
    uint16_t chip_y_norm = priv->chip_y_res ? priv->chip_y_res : 568;

    uint32_t x = (uint32_t)raw_x * tp->config.x_max / chip_y_norm;
    uint32_t y = (uint32_t)raw_y * tp->config.y_max / chip_x_norm;

    if (priv->map_swap_xy) {
        uint32_t tmp = x;
        x = y;
        y = tmp;
    }
    if (priv->map_mirror_x) {
        x = (tp->config.x_max > 0) ? (tp->config.x_max - 1 - x) : 0;
    }
    if (priv->map_mirror_y) {
        y = (tp->config.y_max > 0) ? (tp->config.y_max - 1 - y) : 0;
    }

    if (x >= tp->config.x_max) {
        x = tp->config.x_max - 1;
    }
    if (y >= tp->config.y_max) {
        y = tp->config.y_max - 1;
    }

    *panel_x = (uint16_t)x;
    *panel_y = (uint16_t)y;
}

static esp_err_t esp_lcd_touch_cst3530_read_data(esp_lcd_touch_handle_t tp);
static _Bool esp_lcd_touch_cst3530_get_xy(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength,
                                         uint8_t *point_num, uint8_t max_point_num);
static esp_err_t esp_lcd_touch_cst3530_del(esp_lcd_touch_handle_t tp);
static esp_err_t touch_cst3530_reset(esp_lcd_touch_handle_t tp);
static esp_err_t touch_cst3530_chip_init(esp_lcd_touch_handle_t tp);

esp_err_t esp_lcd_touch_new_i2c_cst3530(const esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *config,
                                        esp_lcd_touch_handle_t *tp)
{
    ESP_RETURN_ON_FALSE(io != NULL, ESP_ERR_INVALID_ARG, TAG, "Touch controller io handle can't be NULL");
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "Pointer to the touch controller configuration can't be NULL");
    ESP_RETURN_ON_FALSE(tp != NULL, ESP_ERR_INVALID_ARG, TAG, "Pointer to the touch controller handle can't be NULL");

    esp_err_t ret = ESP_OK;
    esp_lcd_touch_handle_t cst3530 = calloc(1, sizeof(esp_lcd_touch_t));
    cst3530_priv_t *priv = calloc(1, sizeof(cst3530_priv_t));
    ESP_GOTO_ON_FALSE(cst3530 && priv, ESP_ERR_NO_MEM, err, TAG, "Touch handle malloc failed");

    cst3530->io = io;
    cst3530->read_data = esp_lcd_touch_cst3530_read_data;
    cst3530->get_xy = esp_lcd_touch_cst3530_get_xy;
    cst3530->del = esp_lcd_touch_cst3530_del;
    cst3530->data.lock.owner = portMUX_FREE_VAL;
    memcpy(&cst3530->config, config, sizeof(esp_lcd_touch_config_t));
    cst3530->config.driver_data = priv;

    priv->map_swap_xy = config->flags.swap_xy;
    priv->map_mirror_x = config->flags.mirror_x;
    priv->map_mirror_y = config->flags.mirror_y;
    /* Coordinates are mapped in read_data; disable esp_lcd_touch software adjust */
    cst3530->config.flags.swap_xy = 0;
    cst3530->config.flags.mirror_x = 0;
    cst3530->config.flags.mirror_y = 0;

    struct hyn_ts_data *ts = &priv->ts_data;
    ts->hyn_fuc_used = &cst3530_fuc;
    ts->plat_data.max_touch_num = MAX_POINTS_REPORT;
    ts->plat_data.x_resolution = config->x_max;
    ts->plat_data.y_resolution = config->y_max;
    ts->plat_data.reset_gpio = config->rst_gpio_num;
    ts->plat_data.irq_gpio = config->int_gpio_num;
    ts->plat_data.swap_xy = 0;
    ts->plat_data.reverse_x = 0;
    ts->plat_data.reverse_y = 0;

    if (cst3530->config.int_gpio_num != GPIO_NUM_NC) {
        const gpio_config_t int_gpio_config = {
            .mode = GPIO_MODE_INPUT,
            .intr_type = (cst3530->config.levels.interrupt ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE),
            .pin_bit_mask = BIT64(cst3530->config.int_gpio_num),
            .pull_up_en = 1,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&int_gpio_config), err, TAG, "GPIO intr config failed");

        if (cst3530->config.interrupt_callback) {
            esp_lcd_touch_register_interrupt_callback(cst3530, cst3530->config.interrupt_callback);
        }
    }

    if (cst3530->config.rst_gpio_num != GPIO_NUM_NC) {
        const gpio_config_t rst_gpio_config = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = BIT64(cst3530->config.rst_gpio_num),
        };
        ESP_GOTO_ON_ERROR(gpio_config(&rst_gpio_config), err, TAG, "GPIO reset config failed");
    }

    ESP_GOTO_ON_ERROR(touch_cst3530_reset(cst3530), err, TAG, "Reset failed");

    ESP_GOTO_ON_ERROR(hyn_i2c_init_from_panel_io(io, 400000), err, TAG, "I2C bind failed");
    ESP_GOTO_ON_ERROR(touch_cst3530_chip_init(cst3530), err, TAG, "Chip init failed");

    *tp = cst3530;
    return ESP_OK;

err:
    if (cst3530) {
        esp_lcd_touch_cst3530_del(cst3530);
    } else {
        free(priv);
    }
    ESP_LOGE(TAG, "Initialization failed!");
    return ret;
}

static esp_err_t touch_cst3530_reset(esp_lcd_touch_handle_t tp)
{
    ESP_RETURN_ON_FALSE(tp != NULL, ESP_ERR_INVALID_ARG, TAG, "Touch controller handle can't be NULL");

    if (tp->config.rst_gpio_num != GPIO_NUM_NC) {
        ESP_RETURN_ON_ERROR(gpio_set_level(tp->config.rst_gpio_num, tp->config.levels.reset), TAG, "GPIO set level failed");
        vTaskDelay(pdMS_TO_TICKS(10));
        ESP_RETURN_ON_ERROR(gpio_set_level(tp->config.rst_gpio_num, !tp->config.levels.reset), TAG, "GPIO set level failed");
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    return ESP_OK;
}

static esp_err_t touch_cst3530_chip_init(esp_lcd_touch_handle_t tp)
{
    cst3530_priv_t *priv = tp->config.driver_data;
    ESP_RETURN_ON_FALSE(priv, ESP_ERR_INVALID_STATE, TAG, "driver data is NULL");

    if (priv->ts_data.hyn_fuc_used->tp_chip_init(&priv->ts_data) != 0) {
        return ESP_FAIL;
    }

    priv->chip_x_res = priv->ts_data.hw_info.fw_res_x;
    priv->chip_y_res = priv->ts_data.hw_info.fw_res_y;
    if (priv->chip_x_res == 0 || priv->chip_y_res == 0) {
        priv->chip_x_res = tp->config.x_max;
        priv->chip_y_res = tp->config.y_max;
    }

    ESP_LOGI(TAG, "touch chip res %ux%u -> panel %ux%u",
             priv->chip_x_res, priv->chip_y_res,
             tp->config.x_max, tp->config.y_max);

#ifndef CONFIG_ESP_LCD_TOUCH_CST3530_DISABLE_CHIP_INFO_LOG
    ESP_LOGI(TAG, "fw_project_id:0x%04x chip_type:0x%04x fw_ver:0x%x module_id:0x%x",
             priv->ts_data.hw_info.fw_project_id,
             priv->ts_data.hw_info.fw_chip_type,
             priv->ts_data.hw_info.fw_ver,
             priv->ts_data.hw_info.fw_module_id);
#else
    ESP_LOGI(TAG, "CST3530 initialized");
#endif

    return ESP_OK;
}

static esp_err_t esp_lcd_touch_cst3530_read_data(esp_lcd_touch_handle_t tp)
{
    ESP_RETURN_ON_FALSE(tp != NULL, ESP_ERR_INVALID_ARG, TAG, "Touch controller handle can't be NULL");

    cst3530_priv_t *priv = tp->config.driver_data;
    ESP_RETURN_ON_FALSE(priv, ESP_ERR_INVALID_STATE, TAG, "driver data is NULL");

    if (priv->ts_data.hyn_fuc_used->tp_report() != 0) {
        if (!priv->touch_active) {
            return ESP_FAIL;
        }
    } else {
        struct ts_frame *rp = &priv->ts_data.rp_buf;
        uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

        if (rp->rep_num > 0) {
            cst3530_map_to_panel(tp, rp->pos_info[0].pos_x, rp->pos_info[0].pos_y, &priv->last_x, &priv->last_y);
            priv->last_strength = rp->pos_info[0].pres_z;
            priv->last_track_id = rp->pos_info[0].pos_id;
            priv->touch_active = true;
            priv->last_active_ms = now_ms;
        } else if (rp->touch_lift) {
            priv->touch_active = false;
        } else if (priv->touch_active &&
                   (now_ms - priv->last_active_ms) >= CST3530_TOUCH_RELEASE_TIMEOUT_MS) {
            priv->touch_active = false;
        }
    }

    {
        static uint8_t s_last_points;
        static uint16_t s_last_x;
        static uint16_t s_last_y;
        if (priv->touch_active) {
            if (s_last_points == 0 || s_last_x != priv->last_x || s_last_y != priv->last_y) {
                ESP_LOGI(TAG, "touch: lvgl(%u,%u)%s",
                         priv->last_x, priv->last_y,
                         priv->ts_data.rp_buf.rep_num > 0 ? "" : " [hold]");
                s_last_x = priv->last_x;
                s_last_y = priv->last_y;
            }
            s_last_points = 1;
        } else if (s_last_points != 0) {
            ESP_LOGI(TAG, "touch: released");
            s_last_points = 0;
        }
    }

    portENTER_CRITICAL(&tp->data.lock);
    if (priv->touch_active) {
        tp->data.points = 1;
        tp->data.coords[0].x = priv->last_x;
        tp->data.coords[0].y = priv->last_y;
        tp->data.coords[0].strength = priv->last_strength;
        tp->data.coords[0].track_id = priv->last_track_id;
    } else {
        tp->data.points = 0;
    }
    portEXIT_CRITICAL(&tp->data.lock);

    return ESP_OK;
}

static _Bool esp_lcd_touch_cst3530_get_xy(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength,
                                         uint8_t *point_num, uint8_t max_point_num)
{
    ESP_RETURN_ON_FALSE(tp != NULL, false, TAG, "Touch controller handle can't be NULL");
    ESP_RETURN_ON_FALSE(x != NULL, false, TAG, "Pointer to the x coordinates array can't be NULL");
    ESP_RETURN_ON_FALSE(y != NULL, false, TAG, "Pointer to the y coordinates array can't be NULL");
    ESP_RETURN_ON_FALSE(point_num != NULL, false, TAG, "Pointer to number of touch points can't be NULL");
    ESP_RETURN_ON_FALSE(max_point_num > 0, false, TAG, "Array size must be equal or larger than 1");

    portENTER_CRITICAL(&tp->data.lock);
    *point_num = (tp->data.points > max_point_num ? max_point_num : tp->data.points);
    for (size_t i = 0; i < *point_num; i++) {
        x[i] = tp->data.coords[i].x;
        y[i] = tp->data.coords[i].y;
        if (strength) {
            strength[i] = tp->data.coords[i].strength;
        }
    }
    tp->data.points = 0;
    portEXIT_CRITICAL(&tp->data.lock);

    return (*point_num > 0);
}

static esp_err_t esp_lcd_touch_cst3530_del(esp_lcd_touch_handle_t tp)
{
    ESP_RETURN_ON_FALSE(tp != NULL, ESP_ERR_INVALID_ARG, TAG, "Touch controller handle can't be NULL");

    if (tp->config.int_gpio_num != GPIO_NUM_NC) {
        gpio_reset_pin(tp->config.int_gpio_num);
        if (tp->config.interrupt_callback) {
            gpio_isr_handler_remove(tp->config.int_gpio_num);
        }
    }
    if (tp->config.rst_gpio_num != GPIO_NUM_NC) {
        gpio_reset_pin(tp->config.rst_gpio_num);
    }

    hyn_i2c_deinit();

    if (tp->config.driver_data) {
        free(tp->config.driver_data);
    }

    free(tp);
    return ESP_OK;
}
