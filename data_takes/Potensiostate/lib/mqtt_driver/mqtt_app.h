#ifndef MQTT_APP_H
#define MQTT_APP_H

#include "esp_err.h"
#include <stddef.h>

/**
 * @brief Callback invocado cuando se recibe un comando MQTT completo.
 *        data y data_len corresponden al payload COMPLETO (ya reensamblado).
 *        NO se garantiza terminación en '\0' — usar data_len explícitamente.
 */
typedef void (*mqtt_cmd_callback_t)(const char *data, int data_len);

/**
 * @brief Inicia el cliente MQTT y conecta al broker definido en app_config.h.
 *        Idempotente: re-llamadas retornan ESP_OK sin re-inicializar.
 * @param on_command Callback para comandos entrantes. Puede ser NULL.
 * @return ESP_OK, ESP_FAIL si falla init/registro, o ESP_ERR_INVALID_STATE.
 */
esp_err_t mqtt_app_start(mqtt_cmd_callback_t on_command);

/**
 * @brief Publica datos JSON en MQTT_PUB_TOPIC (QoS 1).
 * @param data_json String JSON terminado en '\0'. No puede ser NULL.
 * @return ESP_OK, ESP_ERR_INVALID_ARG si NULL, ESP_ERR_INVALID_STATE
 *         si no conectado, ESP_FAIL si publish falla.
 */
esp_err_t mqtt_publish_data(const char *data_json);

/**
 * @brief Detiene y destruye el cliente MQTT ordenadamente.
 *        Secuencia: stop → disconnect → destroy (requerida por ESP-IDF).
 * @return ESP_OK.
 */
esp_err_t mqtt_app_stop(void);

#endif 