#include <stdio.h>
#include <string.h>
#include "nvs_flash.h"
#include "esp_log.h"
#include "app_config.h"

#include "wifi_manager.h"
#include "mqtt_app.h"
#include "potenciostato_core.h"

static const char *TAG = "MAIN";

/**
 * Callback invocado por mqtt_app cuando llega un mensaje al topic de comandos.
 * Comandos soportados:
 *   "start" -> inicia un barrido triangular
 *   "stop"  -> detiene la medicion en curso
 */
static void on_mqtt_command(const char *cmd, int cmd_len)
{
    if (cmd_len >= 5 && strncmp(cmd, "start", 5) == 0) {
        ESP_LOGI(TAG, "Comando recibido: START");
        esp_err_t ret = potenciostato_start_measurement();
        if (ret == ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "Ya hay una medicion en curso");
        } else if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error iniciando medicion");
        }

    } else if (cmd_len >= 4 && strncmp(cmd, "stop", 4) == 0) {
        ESP_LOGI(TAG, "Comando recibido: STOP");
        potenciostato_stop_measurement();

    } else {
        ESP_LOGW(TAG, "Comando desconocido: %.*s", cmd_len, cmd);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "--- INICIANDO SISTEMA POTENCIOSTATO (ESP32-C3) ---");

    // Inicializar NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Inicializar WiFi
    ret = wifi_init_sta();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi no inicializado, continuando sin red");
    }

    // Inicializar MQTT con callback de comandos
    ret = mqtt_app_start(on_mqtt_command);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "MQTT no inicializado, continuando sin MQTT");
    }

    // Inicializar hardware del potenciostato
    ret = potenciostato_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error critico inicializando potenciostato, reiniciando...");
        esp_restart();
    }

    ESP_LOGI(TAG, "Sistema listo. Esperando comando 'start' en topic '%s'", MQTT_SUB_TOPIC);
    // app_main retorna, el sistema queda manejado por el timer y los eventos MQTT
}
