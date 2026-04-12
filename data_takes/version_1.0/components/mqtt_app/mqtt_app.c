#include "mqtt_app.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "app_config.h"

static const char *TAG = "MQTT_APP";

static esp_mqtt_client_handle_t s_client = NULL;
static mqtt_cmd_callback_t s_cmd_cb = NULL;

static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Conectado al broker");
        esp_mqtt_client_subscribe(s_client, MQTT_SUB_TOPIC, 1);
        ESP_LOGI(TAG, "Suscrito a %s", MQTT_SUB_TOPIC);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Desconectado del broker");
        break;

    case MQTT_EVENT_DATA:
        if (event->topic_len > 0) {
            ESP_LOGI(TAG, "Mensaje recibido en %.*s: %.*s",
                     event->topic_len, event->topic,
                     event->data_len, event->data);
        }
        if (s_cmd_cb != NULL && event->data_len > 0) {
            s_cmd_cb(event->data, event->data_len);
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "Error MQTT");
        break;

    default:
        break;
    }
}

esp_err_t mqtt_app_start(mqtt_cmd_callback_t on_command)
{
    s_cmd_cb = on_command;

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.uri = MQTT_BROKER_URL,
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "Error creando cliente MQTT");
        return ESP_FAIL;
    }

    esp_err_t ret = esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error registrando handler MQTT");
        return ret;
    }

    ret = esp_mqtt_client_start(s_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error iniciando cliente MQTT");
        return ret;
    }

    ESP_LOGI(TAG, "Cliente MQTT iniciado, conectando a %s", MQTT_BROKER_URL);
    return ESP_OK;
}

esp_err_t mqtt_publish_data(const char *data_json)
{
    if (s_client == NULL) {
        ESP_LOGW(TAG, "Cliente MQTT no iniciado, descartando datos");
        return ESP_ERR_INVALID_STATE;
    }

    int msg_id = esp_mqtt_client_publish(s_client, MQTT_PUB_TOPIC, data_json, 0, 1, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Error publicando datos");
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Publicado msg_id=%d", msg_id);
    return ESP_OK;
}
