#include "esp_log.h"
#include "ps_common.h"
#include "store.h"
#include "net.h"
#include "pub.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "conv ADS1115 @860 SPS: %u us", (unsigned)ps_adc_conv_us(7));
    /* Solo para enlazar store/net/pub (Task 10 los conecta de verdad); sin red solo reintenta WiFi. */
    ESP_ERROR_CHECK(store_init());
    ESP_ERROR_CHECK(pub_start());
    ESP_ERROR_CHECK(net_start(NULL));
}
