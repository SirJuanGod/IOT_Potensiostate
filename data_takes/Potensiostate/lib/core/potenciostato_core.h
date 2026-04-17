#ifndef POTENCIOSTATO_CORE_H
#define POTENCIOSTATO_CORE_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Inicializa el hardware del potenciostato (I2C, SPI, MUX) y
 *        crea la tarea de medición y el timer de disparo.
 */
esp_err_t potenciostato_init(void);

/**
 * @brief Inicia un barrido de onda triangular. No bloqueante.
 * @return ESP_OK, ESP_ERR_INVALID_STATE si ya hay medición en curso.
 */
esp_err_t potenciostato_start_measurement(void);

/**
 * @brief Detiene la medición en curso (si hay alguna).
 */
void potenciostato_stop_measurement(void);

/**
 * @brief Consulta si hay una medición en curso.
 */
bool potenciostato_is_measuring(void);

/**
 * @brief Escribe un valor de 16 bits al DAC AD5662 (Normal Operation).
 */
esp_err_t dac_write(uint16_t code);

/**
 * @brief Lee el resultado de conversión del ADS1115 (transacción combinada).
 */
esp_err_t adc_read(int16_t *raw_value);

#endif /* POTENCIOSTATO_CORE_H */