/**
 * @file mqtt_app.c
 * @brief Cliente MQTT para potenciostato — ESP-IDF v5.x
 */

#include <string.h>
#include "mqtt_app.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "app_config.h"

static const char *TAG = "MQTT_APP";

#define MQTT_CMD_BUF_SIZE   512u

static esp_mqtt_client_handle_t s_client  = NULL;
static mqtt_cmd_callback_t      s_cmd_cb  = NULL;

static char             s_cmd_buf[MQTT_CMD_BUF_SIZE];
static int              s_cmd_buf_offset = 0;
static int              s_cmd_total_len  = 0;

/* Mutex para proteger s_cmd_buf de acceso concurrente */
static SemaphoreHandle_t s_buf_mutex = NULL;


static void mqtt_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {

    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Conectado al broker MQTT");
        {
            int sub_id = esp_mqtt_client_subscribe(s_client, MQTT_SUB_TOPIC, 1);
            if (sub_id < 0) {
                ESP_LOGE(TAG, "Fallo al suscribir a '%s'", MQTT_SUB_TOPIC);
            } else {
                ESP_LOGI(TAG, "Suscrito a '%s' (msg_id=%d)", MQTT_SUB_TOPIC, sub_id);
            }
        }
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Desconectado del broker");
        if (xSemaphoreTake(s_buf_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            s_cmd_buf_offset = 0;
            s_cmd_total_len  = 0;
            xSemaphoreGive(s_buf_mutex);
        }
        break;

    case MQTT_EVENT_DATA:
        /* Proteger buffer de reensamblado */
        if (xSemaphoreTake(s_buf_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGW(TAG, "Timeout mutex — fragmento descartado");
            break;
        }

        if (event->current_data_offset == 0) {
            s_cmd_total_len  = event->total_data_len;
            s_cmd_buf_offset = 0;

            if (event->topic_len > 0) {
                ESP_LOGI(TAG, "Mensaje en '%.*s' (%d bytes)",
                         event->topic_len, event->topic, s_cmd_total_len);
            }

            if (s_cmd_total_len > (int)(MQTT_CMD_BUF_SIZE - 1)) {
                ESP_LOGW(TAG, "Payload (%d B) excede buffer (%d B), descartando",
                         s_cmd_total_len, MQTT_CMD_BUF_SIZE - 1);
                s_cmd_total_len = -1;
                xSemaphoreGive(s_buf_mutex);
                break;
            }
        }

        if (s_cmd_total_len < 0) {
            xSemaphoreGive(s_buf_mutex);
            break;
        }

        if (s_cmd_buf_offset + event->data_len <= (int)(MQTT_CMD_BUF_SIZE - 1)) {
            memcpy(s_cmd_buf + s_cmd_buf_offset, event->data, event->data_len);
            s_cmd_buf_offset += event->data_len;
        }

        if (s_cmd_buf_offset >= s_cmd_total_len) {
            s_cmd_buf[s_cmd_buf_offset] = '\0';
            ESP_LOGD(TAG, "Mensaje completo (%d B): %s",
                     s_cmd_buf_offset, s_cmd_buf);

            /* Llamar callback FUERA del mutex para evitar deadlock */
            int   len = s_cmd_buf_offset;
            char  local_copy[MQTT_CMD_BUF_SIZE];
            memcpy(local_copy, s_cmd_buf, len + 1);

            s_cmd_buf_offset = 0;
            s_cmd_total_len  = 0;
            xSemaphoreGive(s_buf_mutex);

            if (s_cmd_cb != NULL) {
                s_cmd_cb(local_copy, len);  /* callback con copia local */
            }
            break;  /* ya liberamos el mutex arriba */
        }

        xSemaphoreGive(s_buf_mutex);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "Error MQTT — tipo: %d", event->error_handle->error_type);
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGE(TAG, "  ESP-TLS: 0x%x | errno: %d",
                     event->error_handle->esp_tls_last_esp_err,
                     event->error_handle->esp_transport_sock_errno);
        } else if (event->error_handle->error_type ==
                   MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            ESP_LOGE(TAG, "  Conexión rechazada, código: %d",
                     event->error_handle->connect_return_code);
        }
        break;

    default:
        break;
    }
}


esp_err_t mqtt_app_start(mqtt_cmd_callback_t on_command)
{
    if (s_client != NULL) {
        ESP_LOGW(TAG, "Cliente MQTT ya activo");
        return ESP_OK;
    }

    /* Crear mutex antes de iniciar el cliente */
    if (s_buf_mutex == NULL) {
        s_buf_mutex = xSemaphoreCreateMutex();
        if (s_buf_mutex == NULL) {
            ESP_LOGE(TAG, "Fallo al crear mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    s_cmd_cb         = on_command;
    s_cmd_buf_offset = 0;
    s_cmd_total_len  = 0;

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URL,
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "Fallo al crear cliente MQTT");
        return ESP_FAIL;
    }

    esp_err_t ret = esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                                    mqtt_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo handler: %s", esp_err_to_name(ret));
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        return ret;
    }

    ret = esp_mqtt_client_start(s_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo start: %s", esp_err_to_name(ret));
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "MQTT iniciado → %s", MQTT_BROKER_URL);
    return ESP_OK;
}


esp_err_t mqtt_publish_data(const char *data_json)
{
    if (data_json == NULL) return ESP_ERR_INVALID_ARG;

    if (s_client == NULL) {
        ESP_LOGW(TAG, "Cliente no iniciado");
        return ESP_ERR_INVALID_STATE;
    }

    int msg_id = esp_mqtt_client_publish(s_client, MQTT_PUB_TOPIC,
                                          data_json, 0, 1, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Fallo publish");
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Publicado en '%s' (msg_id=%d)", MQTT_PUB_TOPIC, msg_id);
    return ESP_OK;
}


esp_err_t mqtt_app_stop(void)
{
    if (s_client == NULL) return ESP_OK;

    /* Orden correcto: disconnect → stop → destroy */
    esp_mqtt_client_disconnect(s_client);
    esp_mqtt_client_stop(s_client);
    esp_mqtt_client_destroy(s_client);

    s_client         = NULL;
    s_cmd_cb         = NULL;
    s_cmd_buf_offset = 0;
    s_cmd_total_len  = 0;

    if (s_buf_mutex != NULL) {
        vSemaphoreDelete(s_buf_mutex);
        s_buf_mutex = NULL;
    }

    ESP_LOGI(TAG, "MQTT detenido");
    return ESP_OK;
}