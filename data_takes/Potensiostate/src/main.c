#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "app_config.h"
#include "wifi_manager.h"
#include "mqtt_app.h"
#include "potenciostato_core.h"

static const char *TAG = "MAIN";

/* ── Callback MQTT ───────────────────────────────────────────────────────── */
static void on_mqtt_command(const char *cmd, int cmd_len)
{
    if (cmd_len >= 5 && strncmp(cmd, "start", 5) == 0) {
        ESP_LOGI(TAG, "Comando: START");
        esp_err_t ret = potenciostato_start_measurement();
        if (ret == ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "Medicion ya en curso");
        } else if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error iniciando medicion: %s", esp_err_to_name(ret));
        }

    } else if (cmd_len >= 4 && strncmp(cmd, "stop", 4) == 0) {
        ESP_LOGI(TAG, "Comando: STOP");
        potenciostato_stop_measurement();

    } else {
        ESP_LOGW(TAG, "Comando desconocido: %.*s", cmd_len, cmd);
    }
}

/* ── app_main ────────────────────────────────────────────────────────────── */
void app_main(void)
{
    ESP_LOGI(TAG, "=== POTENCIOSTATO ESP32-C3 — INICIO ===");

    /* [1] NVS — requerido por WiFi */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS dañado, borrando...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "[1/4] NVS OK");

    /* Hardware primero — antes de habilitar comandos externos */
    ret = potenciostato_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[2/4] Error critico en potenciostato: %s",
                 esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(100));   // vaciar buffer UART antes de restart
        esp_restart();
    }
    ESP_LOGI(TAG, "[2/4] Potenciostato OK");

    /* [3] WiFi — asíncrono, wifi_manager debe señalizar cuando conecte */
    ret = wifi_init_sta();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "[3/4] WiFi no disponible — modo offline");
        ESP_LOGI(TAG, "Sistema funcionando sin red. "
                      "Comandos MQTT no disponibles.");
        return;   // Continuar sin MQTT es válido para modo standalone
    }
    ESP_LOGI(TAG, "[3/4] WiFi OK");

    /* [4] MQTT — último, solo cuando WiFi ya está conectado */
    ret = mqtt_app_start(on_mqtt_command);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "[4/4] MQTT no disponible — modo offline");
    } else {
        ESP_LOGI(TAG, "[4/4] MQTT OK — topic cmd: %s", MQTT_SUB_TOPIC);
    }

    ESP_LOGI(TAG, "=== SISTEMA LISTO — esperando comando 'start' ===");
    /* app_main retorna — FreeRTOS mantiene vivas meas_task y eventos MQTT */
}