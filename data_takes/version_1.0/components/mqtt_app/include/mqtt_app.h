#ifndef MQTT_APP_H
#define MQTT_APP_H

#include "esp_err.h"

typedef void (*mqtt_cmd_callback_t)(const char *cmd, int cmd_len);

/**
 * @brief Inicia el cliente MQTT, conecta al broker y se suscribe al topic de comandos.
 * @param on_command Callback que se invoca cuando llega un mensaje al topic de comandos.
 */
esp_err_t mqtt_app_start(mqtt_cmd_callback_t on_command);

/**
 * @brief Publica datos JSON en el topic de publicacion.
 */
esp_err_t mqtt_publish_data(const char *data_json);

#endif // MQTT_APP_H
