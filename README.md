# ESP LCD Touch CST3530

[![Component Registry](https://components.espressif.com/components/viewesmart/esp_lcd_touch_cst3530/badge.svg)](https://components.espressif.com/components/viewesmart/esp_lcd_touch_cst3530)

| | |
|---|---|
| **Author** | Ayang |
| **Organization** | Shenzhen Viewe Technology Co., Ltd. |
| **Copyright** | © 2026 Shenzhen Viewe Technology Co., Ltd. |
| **License** | Apache-2.0 |
| **Version** | 1.0.2 |

ESP LCD touch driver for **CST3530** (HYN **cst66xx** series) capacitive touch controllers. The component implements the standard [`esp_lcd_touch`](https://components.espressif.com/components/espressif/esp_lcd_touch) API and can be used with LVGL (via `esp_lvgl_adapter`) or any custom input stack.

The built-in HYN low-level driver handles chip initialization, touch report parsing, and coordinate scaling for common CO5300 MIPI panels (568×1210). Other panel sizes are supported by setting `x_max` / `y_max` in `esp_lcd_touch_config_t`.

## Features

- [x] I2C interface via `esp_lcd_panel_io` + `i2c_master` bus (ESP-IDF v5.5+)
- [x] Standard `esp_lcd_touch` API (`read_data`, `get_xy`, interrupt callback)
- [x] Automatic raw-to-panel coordinate mapping using chip-reported RX/TX resolution
- [x] Touch state hold (chip reports only on contact change)
- [x] Reliable release detection (`touch_lift` + timeout fallback)
- [x] Optional swap / mirror in driver (GT911-style `esp_lcd_touch` software adjust is disabled internally)
- [x] Polling and GPIO interrupt modes
- [ ] Hardware sleep / wake (not implemented)
- [ ] Multi-touch gestures (single-point pointer use case only)

## Supported hardware

| Item | Value |
|------|--------|
| Touch IC | CST3530 (HYN cst66xx firmware) |
| Interface | I2C |
| 7-bit address | `0x58` |
| Typical panel pairing | CO5300 568×1210 MIPI DSI |

> **Note:** The driver has been validated on **ESP32-P4** with CO5300 + CST3530. Other Espressif chips with `i2c_master` and `esp_lcd_touch` should work, but may require pin / timing adjustments.

## Dependencies

- ESP-IDF `>= 5.5.0`
- [`esp_lcd_touch`](https://components.espressif.com/components/espressif/esp_lcd_touch) `^1.2.0`

## Add to your project

Add to your project's `main/idf_component.yml` (or the component that uses this driver):

```yaml
dependencies:
  viewesmart/esp_lcd_touch_cst3530:
    version: "^1.0.2"
```

## Hardware connection

Typical wiring (adjust GPIOs for your board):

| CST3530 | ESP32 |
|---------|-------|
| SDA | I2C SDA |
| SCL | I2C SCL |
| RST | GPIO (output, active level configurable) |
| INT | GPIO (input, optional — use polling if unconnected) |
| VCC / GND | 3.3 V / GND |

External I2C pull-ups are required if not present on the module.

## Usage

### 1. Create I2C bus and panel IO

**Important:** set `user_ctx` on the panel IO config to your `i2c_master_bus_handle_t`. The HYN layer uses it for raw I2C transfers.

```c
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_cst3530.h"

#define TOUCH_I2C_SCL    GPIO_NUM_8
#define TOUCH_I2C_SDA    GPIO_NUM_7
#define TOUCH_RST        GPIO_NUM_20
#define TOUCH_INT        GPIO_NUM_21   /* or GPIO_NUM_NC for polling */

static esp_err_t touch_init(esp_lcd_touch_handle_t *out_touch)
{
    i2c_master_bus_handle_t i2c_bus = NULL;
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = TOUCH_I2C_SDA,
        .scl_io_num = TOUCH_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &i2c_bus));

    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_CST3530_CONFIG();
    tp_io_cfg.scl_speed_hz = 400000;
    tp_io_cfg.user_ctx = i2c_bus;   /* required */
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_cfg, &tp_io));

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = 568,
        .y_max = 1210,
        .rst_gpio_num = TOUCH_RST,
        .int_gpio_num = TOUCH_INT,  /* GPIO_NUM_NC → polling mode */
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
    };

    return esp_lcd_touch_new_i2c_cst3530(tp_io, &tp_cfg, out_touch);
}
```

### 2. Read touch data

Same as other `esp_lcd_touch` drivers:

```c
esp_lcd_touch_read_data(tp);
uint16_t x[1], y[1], strength[1];
uint8_t point_num = 0;
if (esp_lcd_touch_get_coordinates(tp, x, y, strength, &point_num, 1)) {
    /* (x[0], y[0]) in panel coordinates */
}
```

Or use `esp_lcd_touch_get_data()` if you need track IDs.

### 3. LVGL integration

Register with [`esp_lvgl_adapter`](https://components.espressif.com/components/espressif/esp_lvgl_adapter):

```c
esp_lv_adapter_touch_config_t touch_cfg = {
    .disp = disp,
    .handle = tp,
};
lv_indev_t *indev = esp_lv_adapter_register_touch(&touch_cfg);
```

## Configuration (Kconfig)

| Option | Description | Default |
|--------|-------------|---------|
| `CONFIG_ESP_LCD_TOUCH_CST3530_DISABLE_CHIP_INFO_LOG` | Skip firmware/chip info log on init | disabled |

## Coordinate mapping

CST3530 reports sensor resolution via HYN firmware, e.g. `fw_res_x=1210`, `fw_res_y=568` (RX × TX channel counts). **These are not the same as the touch coordinate range.**

The driver maps:

```
panel_x = raw_x × x_max / fw_res_y
panel_y = raw_y × y_max / fw_res_x
```

For CO5300 (568×1210), raw X is in ~0…568 and raw Y in ~0…1210, which maps 1:1 to panel pixels.

If coordinates are wrong on a different panel:

1. Log chip info at boot (`res-x`, `res-y`).
2. Compare raw vs panel coordinates at screen corners.
3. Adjust `x_max` / `y_max`, or use `flags.swap_xy` / `mirror_x` / `mirror_y` in `esp_lcd_touch_config_t` (applied inside this driver, not by `esp_lcd_touch` software adjust).

## Display rotation

Do **not** blindly reuse GT911 / GT1151 MIPI mirror rules for CST3530. On some setups, enabling default MIPI `mirror_x` + `mirror_y` flips the origin to the opposite corner.

Recommended approach:

- Set `mirror_x` / `mirror_y` to `false` unless you verify them on hardware.
- Use `swap_xy` only when the display rotation requires it.
- Keep `x_max` / `y_max` aligned with the **logical** LVGL resolution after rotation.

## Polling vs interrupt

| Mode | Setup | When to use |
|------|--------|-------------|
| **Polling** | `int_gpio_num = GPIO_NUM_NC` | INT pin not wired or not verified |
| **Interrupt** | Connect INT, set `int_gpio_num`, register `esp_lcd_touch` interrupt callback | Lower latency, less I2C traffic |

The driver includes **touch state hold** so polling works correctly even though the chip only reports on contact change.

## Implementation notes

### Touch event encoding (HYN cst66xx)

| `event` field | Meaning |
|---------------|---------|
| `0` | Finger up / no contact |
| non-zero | Finger down or move |

Do not invert this mapping when modifying `hyn_cst66xx.c`.

### Release detection

- Normal release: all touch points have `event == 0` → `touch_lift` flag.
- Missed release packet: driver clears active state after **80 ms** without new coordinates.

### I2C panel IO

This driver uses a custom HYN I2C path bound to `panel_io->user_ctx` (bus handle). The standard `esp_lcd_panel_io` read/write API alone is not sufficient; always pass the bus handle via `user_ctx`.

## Directory structure

```
esp_lcd_touch_cst3530/
├── include/
│   └── esp_lcd_touch_cst3530.h   Public API
├── private/hyn/                  HYN cst66xx low-level driver (internal)
├── esp_lcd_touch_cst3530.c       esp_lcd_touch port layer
├── Kconfig
├── CMakeLists.txt
└── idf_component.yml
```

## Troubleshooting

| Symptom | Possible cause | Suggestion |
|---------|----------------|------------|
| No touch response | INT mode but no IRQ | Use polling (`GPIO_NUM_NC`) |
| X/Y swapped or mirrored | Wrong rotation flags | Disable GT911-style mirrors; tune swap/mirror |
| X only reaches half width | Wrong scale denominator | Use per-axis scaling (see above) |
| Must tap twice | Missing press/release sequence | Ensure latest driver (state hold + release timeout) |
| I2C errors | Missing pull-ups / wrong address | Check wiring, address `0x58`, 400 kHz |

## License

Copyright © 2026 **Shenzhen Viewe Technology Co., Ltd.**

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE) for the full license text.

## Contributing

Issues and pull requests are welcome. When submitting, please include:

- Chip / module part number
- Panel controller and resolution
- ESP-IDF version and target chip
- Raw and mapped coordinates at screen corners (if reporting coordinate issues)
