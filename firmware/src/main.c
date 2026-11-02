#include "esp_log.h"
#include "ps_common.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "conv ADS1115 @860 SPS: %u us", (unsigned)ps_adc_conv_us(7));
}
