#include "hyn_core.h"
#include "hyn_panel_io_i2c.h"
#include "esp_check.h"

#define I2C_XFER_TIMEOUT_MS  1000

#define CST3530_MAIN_I2C_ADDR  (0x58)
#define CST3530_BOOT_I2C_ADDR  (0x5A)

typedef struct {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev_main;
    i2c_master_dev_handle_t dev_boot;
    i2c_master_dev_handle_t active_dev;
    uint32_t clk_hz;
    bool boot_dev_added;
} hyn_i2c_ctx_t;

static hyn_i2c_ctx_t s_i2c;

esp_err_t hyn_i2c_init_from_panel_io(esp_lcd_panel_io_handle_t io, uint32_t clk_hz)
{
    ESP_RETURN_ON_FALSE(io, ESP_ERR_INVALID_ARG, "hyn_i2c", "panel io is NULL");

    hyn_lcd_panel_io_i2c_t *panel_io = hyn_lcd_panel_io_i2c_from_handle(io);
    ESP_RETURN_ON_FALSE(panel_io->user_ctx, ESP_ERR_INVALID_STATE, "hyn_i2c",
                        "panel io user_ctx must be i2c_master_bus_handle_t");

    memset(&s_i2c, 0, sizeof(s_i2c));
    s_i2c.bus = (i2c_master_bus_handle_t)panel_io->user_ctx;
    s_i2c.dev_main = panel_io->i2c_handle;
    s_i2c.active_dev = s_i2c.dev_main;
    s_i2c.clk_hz = clk_hz ? clk_hz : 400000;

    i2c_device_config_t boot_cfg = {
        .device_address = CST3530_BOOT_I2C_ADDR,
        .scl_speed_hz = s_i2c.clk_hz,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c.bus, &boot_cfg, &s_i2c.dev_boot),
                        "hyn_i2c", "add boot i2c device failed");
    s_i2c.boot_dev_added = true;

    return ESP_OK;
}

void hyn_i2c_deinit(void)
{
    if (s_i2c.boot_dev_added && s_i2c.dev_boot) {
        i2c_master_bus_rm_device(s_i2c.dev_boot);
        s_i2c.dev_boot = NULL;
        s_i2c.boot_dev_added = false;
    }
    memset(&s_i2c, 0, sizeof(s_i2c));
}

static i2c_master_dev_handle_t hyn_i2c_get_active_dev(uint8_t addr)
{
    if (addr == CST3530_BOOT_I2C_ADDR) {
        return s_i2c.dev_boot;
    }
    return s_i2c.dev_main;
}

int hyn_write_data(struct hyn_ts_data *ts_data, u8 *buf, u8 reg_len, u16 len)
{
    i2c_master_dev_handle_t dev = hyn_i2c_get_active_dev(ts_data->salve_addr);
    int ret = i2c_master_transmit(dev, buf, len, pdMS_TO_TICKS(I2C_XFER_TIMEOUT_MS));
    return ret == ESP_OK ? 0 : -1;
}

int hyn_read_data(struct hyn_ts_data *ts_data, u8 *buf, u16 len)
{
    i2c_master_dev_handle_t dev = hyn_i2c_get_active_dev(ts_data->salve_addr);
    int ret = i2c_master_receive(dev, buf, len, pdMS_TO_TICKS(I2C_XFER_TIMEOUT_MS));
    return ret == ESP_OK ? 0 : -1;
}

int hyn_wr_reg(struct hyn_ts_data *ts_data, u32 reg_addr, u8 reg_len, u8 *rbuf, u16 rlen)
{
    u8 wbuf[4];
    reg_len = reg_len & 0x0F;
    memset(wbuf, 0, sizeof(wbuf));

    int i = reg_len;
    while (i) {
        i--;
        wbuf[i] = reg_addr;
        reg_addr >>= 8;
    }

    i2c_master_dev_handle_t dev = hyn_i2c_get_active_dev(ts_data->salve_addr);
    esp_err_t ret = i2c_master_transmit(dev, wbuf, reg_len, pdMS_TO_TICKS(I2C_XFER_TIMEOUT_MS));
    if (rlen && ret == ESP_OK) {
        ret = i2c_master_receive(dev, rbuf, rlen, pdMS_TO_TICKS(I2C_XFER_TIMEOUT_MS));
    }
    return ret == ESP_OK ? 0 : -1;
}
