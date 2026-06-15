#include "hyn_core.h"

void hyn_delay_ms(int cnt)
{
    vTaskDelay(pdMS_TO_TICKS(cnt));
}

int gpio_set_value(uint32_t gpio_id, bool vlue)
{
    gpio_set_level(gpio_id, vlue);
    return 0;
}

bool gpio_get_value(uint32_t gpio_id)
{
    return gpio_get_level(gpio_id);
}
