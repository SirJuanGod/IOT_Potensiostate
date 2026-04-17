#ifndef MUX_DRIVER_H
#define MUX_DRIVER_H

#include "esp_err.h"
#include <stdint.h>

#define MUX_CHANNEL_NONE  0xFFu   /**< Sentinel: ningún canal seleccionado aún */
#define MUX_MAX_CHANNEL   7u      /**< MAX4558: 8 canales (0–7)                */

/**
 * @brief Inicializa los GPIO de selección del MAX4558 (A, B, C).
 *        ENABLE conectado a GND físicamente → siempre habilitado.
 *        Selecciona canal 0 al finalizar.
 *        Idempotente: re-llamadas seguras.
 * @return ESP_OK, ESP_ERR_INVALID_STATE si ya inicializado, o código GPIO.
 */
esp_err_t mux_driver_init(void);

/**
 * @brief Selecciona un canal del MAX4558 (0–7).
 *        Si el canal ya está activo, no realiza escrituras GPIO (sin glitch).
 *
 * @param channel Canal 0–7. Codificación binaria: A=bit0, B=bit1, C=bit2.
 *                Ref: MAX4558 Datasheet, Table 1 (Truth Table).
 * @return ESP_OK, ESP_ERR_INVALID_ARG si channel > 7,
 *         ESP_ERR_INVALID_STATE si mux_driver_init() no fue llamado.
 */
esp_err_t mux_select_channel(uint8_t channel);

/**
 * @brief Retorna el canal actualmente seleccionado.
 * @return Canal activo (0–7), o MUX_CHANNEL_NONE si no inicializado.
 */
uint8_t mux_get_current_channel(void);

/**
 * @brief Libera recursos. Deja GPIOs como input flotante.
 * @return ESP_OK.
 */
esp_err_t mux_driver_deinit(void);

#endif