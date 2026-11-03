/* WiFi STA + cliente MQTT (esp-mqtt). Topics: potenciostato/<device_id>/{cmd,status,data,result}. */
#pragma once
#include <stdbool.h>
#include "esp_err.h"

/* Se invoca en el contexto de la tarea de esp-mqtt: no debe bloquear; solo debe copiar o encolar. */
typedef void (*net_cmd_cb_t)(const char *data, int len);

esp_err_t net_start(net_cmd_cb_t cb);   /* no bloquea; MQTT arranca con IP_EVENT_STA_GOT_IP. Requiere store_init (NVS) antes */
esp_err_t net_publish(const char *sub, const char *data, int len, bool retain);   /* sub: "status"|"data"|"result"; ESP_ERR_INVALID_STATE si no hay conexión */
bool      net_connected(void);
void      net_set_busy_query(bool (*fn)(void));   /* al conectar se publica "running" (retenido) si fn() es true, si no "idle" */
