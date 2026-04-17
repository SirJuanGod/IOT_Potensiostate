/**
 * @file wifi_manager.c
 * @brief WiFi Station Manager — ESP-IDF v5.x
 */

#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "app_config.h"

static const char *TAG = "WIFI_MGR";

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

#ifndef WIFI_MAX_RETRY
  #define WIFI_MAX_RETRY    5
#endif

/* Timeout de espera WiFi — no bloquear indefinidamente
 * 10s es suficiente para cualquier AP doméstico o de laboratorio */
#define WIFI_CONNECT_TIMEOUT_MS   10000

static EventGroupHandle_t            s_wifi_event_group = NULL;
static esp_event_handler_instance_t  s_handler_wifi     = NULL;
static esp_event_handler_instance_t  s_handler_ip       = NULL;
static bool                          s_initialized      = false;
static bool                          s_connected        = false;
static int                           s_retry_count      = 0;


static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {

        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            ESP_LOGI(TAG, "WiFi iniciado, conectando...");
            break;

        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "Asociado al AP '%s'", WIFI_SSID);
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            s_connected = false;
            if (s_retry_count < WIFI_MAX_RETRY) {
                esp_wifi_connect();
                s_retry_count++;
                ESP_LOGW(TAG, "Reintentando (%d/%d)...",
                         s_retry_count, WIFI_MAX_RETRY);
            } else {
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                ESP_LOGE(TAG, "Fallo tras %d reintentos", WIFI_MAX_RETRY);
            }
            break;

        default:
            break;
        }

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "IP obtenida: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_count = 0;
        s_connected   = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}


esp_err_t wifi_init_sta(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "WiFi ya inicializado");
        return s_connected ? ESP_OK : ESP_FAIL;
    }

    /* NVS NO se inicializa aquí — es responsabilidad de main.c.
     * El .h ya lo documenta como prerequisito. Doble init causa
     * ESP_ERR_NVS_INVALID_STATE en ESP-IDF v5.x */

    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo esp_netif_init: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Fallo event_loop: %s", esp_err_to_name(ret));
        return ret;
    }

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Fallo EventGroup");
        return ESP_ERR_NO_MEM;
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo esp_wifi_init: %s", esp_err_to_name(ret));
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
        return ret;
    }

    ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL,
                                               &s_handler_wifi);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "Fallo WIFI handler"); return ret; }

    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler, NULL,
                                               &s_handler_ip);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "Fallo IP handler"); return ret; }

    wifi_config_t wifi_config = {
        .sta = {
            .ssid               = WIFI_SSID,
            .password           = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable  = true,
                .required = false,
            },
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Marcar inicializado DESPUÉS de esp_wifi_start(),
     * independiente de si conecta o no — el stack ya está activo */
    s_initialized = true;

    ESP_LOGI(TAG, "Conectando a '%s'...", WIFI_SSID);

    /* Timeout finito — no bloquear si el AP no responde */
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS)
    );

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Conectado a '%s'", WIFI_SSID);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "No se pudo conectar tras %d intentos", WIFI_MAX_RETRY);
        return ESP_FAIL;
    }

    /* Timeout sin respuesta del AP */
    ESP_LOGE(TAG, "Timeout esperando conexión WiFi (%dms)",
             WIFI_CONNECT_TIMEOUT_MS);
    return ESP_FAIL;
}


bool wifi_is_connected(void)
{
    return s_connected;
}


esp_err_t wifi_manager_deinit(void)
{
    if (!s_initialized) return ESP_OK;

    if (s_handler_wifi) {
        esp_event_handler_instance_unregister(WIFI_EVENT,
                                               ESP_EVENT_ANY_ID,
                                               s_handler_wifi);
        s_handler_wifi = NULL;
    }
    if (s_handler_ip) {
        esp_event_handler_instance_unregister(IP_EVENT,
                                               IP_EVENT_STA_GOT_IP,
                                               s_handler_ip);
        s_handler_ip = NULL;
    }

    esp_wifi_stop();
    esp_wifi_deinit();

    if (s_wifi_event_group) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }

    s_initialized = false;
    s_connected   = false;
    s_retry_count = 0;

    ESP_LOGI(TAG, "WiFi detenido");
    return ESP_OK;
}