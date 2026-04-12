#include "wifi_manager.h"
#include "esp_log.h"
#include "app_config.h"

static const char *TAG = "WIFI_MGR";

esp_err_t wifi_init_sta(void) {
    ESP_LOGI(TAG, "Conectando a %s... (Pendiente implementar logica esp_wifi)", WIFI_SSID);
    // TODO: Implementar esp_wifi_init, start y connect
    return ESP_OK;
}
