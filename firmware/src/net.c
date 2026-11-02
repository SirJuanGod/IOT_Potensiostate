#include "net.h"
#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "sdkconfig.h"
#include "protocol.h"

#define TOPIC_PREFIX "potenciostato/" CONFIG_PS_DEVICE_ID "/"
#define BACKOFF_MAX_S 30
#define OUTBOX_LIMIT_B (24 * 1024)   /* evita agotar la RAM si el broker tarda en confirmar (QoS 1); publish devuelve < 0 */

static const char *TAG = "net";
static const char LWT_MSG[] = "{\"state\":\"offline\"}";
static const char IDLE_MSG[] = "{\"state\":\"idle\"}";

static net_cmd_cb_t s_cb;
static esp_mqtt_client_handle_t s_client;
static esp_timer_handle_t s_retry;
static volatile bool s_connected;
static uint32_t s_backoff_s = 1;

static void retry_cb(void *arg)
{
    (void)arg;
    esp_wifi_connect();
}

static void on_mqtt(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base;
    esp_mqtt_event_handle_t ev = data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "MQTT conectado");
        esp_mqtt_client_publish(s_client, TOPIC_PREFIX "status", IDLE_MSG, 0, 1, 1);
        esp_mqtt_client_subscribe(s_client, TOPIC_PREFIX "cmd", 1);
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "MQTT desconectado");
        break;
    case MQTT_EVENT_DATA:
        if (ev->total_data_len > ev->data_len || ev->current_data_offset > 0) {
            /* Mensaje fragmentado: se descarta entero; se avisa una sola vez, en el primer fragmento. */
            if (ev->current_data_offset == 0) {
                char buf[128];
                ps_err_t err;
                ps_err_set(&err, PS_ERR_PARSE, "cmd", 0, "mensaje fragmentado");
                int n = proto_write_status(buf, sizeof buf, "error", &err);
                if (n > 0) net_publish("status", buf, n, false);
            }
        } else if (s_cb) {
            s_cb(ev->data, ev->data_len);
        }
        break;
    default:
        break;
    }
}

static void start_mqtt(void)
{
    if (s_client) return;   /* esp-mqtt reconecta por sí solo */
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = CONFIG_PS_MQTT_URI,
        .buffer.size = 4096,
        .outbox.limit = OUTBOX_LIMIT_B,
        .session.last_will = {
            .topic = TOPIC_PREFIX "status", .msg = LWT_MSG, .msg_len = sizeof LWT_MSG - 1, .qos = 1, .retain = 1,
        },
    };
    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) { ESP_LOGE(TAG, "esp_mqtt_client_init falló"); return; }
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, on_mqtt, NULL);
    esp_mqtt_client_start(s_client);
}

static void on_wifi_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        /* Backoff 1, 2, 4, ... 30 s con un esp_timer one-shot; sin bucles bloqueantes. */
        ESP_LOGW(TAG, "WiFi caído, reintento en %u s", (unsigned)s_backoff_s);
        esp_timer_start_once(s_retry, (uint64_t)s_backoff_s * 1000000ULL);   /* INVALID_STATE si ya está armado: se ignora */
        s_backoff_s = s_backoff_s * 2 > BACKOFF_MAX_S ? BACKOFF_MAX_S : s_backoff_s * 2;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_backoff_s = 1;
        ESP_LOGI(TAG, "WiFi con IP");
        start_mqtt();
    }
}

esp_err_t net_start(net_cmd_cb_t cb)
{
    s_cb = cb;
    const esp_timer_create_args_t targs = { .callback = retry_cb, .name = "wifi_retry" };
    esp_err_t e = esp_timer_create(&targs, &s_retry);
    if (e != ESP_OK) return e;
    e = esp_netif_init();
    if (e != ESP_OK) return e;
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;   /* INVALID_STATE: ya existe */
    if (!esp_netif_create_default_wifi_sta()) return ESP_FAIL;

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    if ((e = esp_wifi_init(&wcfg)) != ESP_OK) return e;
    if ((e = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_ip, NULL)) != ESP_OK) return e;
    if ((e = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_ip, NULL)) != ESP_OK) return e;

    wifi_config_t sta = { 0 };
    strlcpy((char *)sta.sta.ssid, CONFIG_PS_WIFI_SSID, sizeof sta.sta.ssid);
    strlcpy((char *)sta.sta.password, CONFIG_PS_WIFI_PASSWORD, sizeof sta.sta.password);
    if ((e = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK) return e;
    if ((e = esp_wifi_set_config(WIFI_IF_STA, &sta)) != ESP_OK) return e;
    return esp_wifi_start();
}

esp_err_t net_publish(const char *sub, const char *data, int len, bool retain)
{
    if (!s_connected || !s_client) return ESP_ERR_INVALID_STATE;
    char topic[64];
    if (snprintf(topic, sizeof topic, TOPIC_PREFIX "%s", sub) >= (int)sizeof topic) return ESP_ERR_INVALID_ARG;
    return esp_mqtt_client_publish(s_client, topic, data, len, 1, retain) < 0 ? ESP_FAIL : ESP_OK;
}

bool net_connected(void)
{
    return s_connected;
}
