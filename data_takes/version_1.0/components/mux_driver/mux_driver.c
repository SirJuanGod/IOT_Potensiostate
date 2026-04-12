#include "mux_driver.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "app_config.h"

static const char *TAG = "MUX_DRIVER";

esp_err_t mux_driver_init(void) {
    const gpio_num_t pins[] = {MUX_SEL_A_IO, MUX_SEL_B_IO, MUX_SEL_C_IO};

    for (int i = 0; i < 3; i++) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << pins[i]),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t ret = gpio_config(&io_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error configurando GPIO %d del MUX", pins[i]);
            return ret;
        }
        gpio_set_level(pins[i], 0);
    }

    ESP_LOGI(TAG, "MAX4558 MUX inicializado. A=%d B=%d C=%d (ENABLE a GND)",
             MUX_SEL_A_IO, MUX_SEL_B_IO, MUX_SEL_C_IO);
    return ESP_OK;
}

esp_err_t mux_select_channel(uint8_t channel) {
    if (channel > 7) {
        ESP_LOGE(TAG, "Canal MUX invalido: %d (max 7)", channel);
        return ESP_ERR_INVALID_ARG;
    }

    gpio_set_level(MUX_SEL_A_IO, (channel >> 0) & 0x01);
    gpio_set_level(MUX_SEL_B_IO, (channel >> 1) & 0x01);
    gpio_set_level(MUX_SEL_C_IO, (channel >> 2) & 0x01);

    ESP_LOGD(TAG, "MUX canal seleccionado: %d", channel);
    return ESP_OK;
}
