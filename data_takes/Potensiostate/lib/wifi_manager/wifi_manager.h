#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Inicializa el stack WiFi y conecta en modo Station.
 *        Bloqueante hasta obtener IP o agotar reintentos (definido en app_config.h).
 *        Idempotente: re-llamadas retornan ESP_OK si ya está conectado.
 *
 *        Requisito: NVS debe estar inicializado antes de llamar esta función.
 *        Si app_config.h no lo inicializa, esta función lo hace internamente.
 *
 * @return ESP_OK si obtuvo IP exitosamente.
 *         ESP_FAIL si agotó reintentos sin conectar.
 */
esp_err_t wifi_init_sta(void);

/**
 * @brief Retorna true si hay conexión activa con IP asignada.
 */
bool wifi_is_connected(void);

/**
 * @brief Detiene el stack WiFi y libera recursos.
 * @return ESP_OK.
 */
esp_err_t wifi_manager_deinit(void);

#endif /* WIFI_MANAGER_H */