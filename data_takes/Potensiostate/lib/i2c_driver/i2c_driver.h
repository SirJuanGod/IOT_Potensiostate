#ifndef I2C_DRIVER_H
#define I2C_DRIVER_H

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

/**
 * @brief Inicializa el bus I2C maestro (driver legacy ESP-IDF).
 *        Idempotente: si ya está instalado, retorna ESP_OK sin re-instalar.
 *        Pines y frecuencia definidos en app_config.h.
 * @note  Requiere pull-ups externos 4.7kΩ para operación correcta a 400kHz.
 *        Pull-ups internos habilitados solo como respaldo.
 * @return ESP_OK, o código de error de i2c_param_config / i2c_driver_install.
 */
esp_err_t i2c_master_init(void);

/**
 * @brief Escribe bytes a un dispositivo I2C.
 * @param dev_addr  Dirección I2C de 7 bits del esclavo.
 * @param data_wr   Buffer con datos a enviar. No puede ser NULL.
 * @param size      Número de bytes a enviar. Debe ser > 0.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_TIMEOUT, u otro error I2C.
 */
esp_err_t i2c_master_write_bytes(uint8_t dev_addr,
                                  const uint8_t *data_wr,
                                  size_t size);

/**
 * @brief Lee bytes de un dispositivo I2C.
 * @param dev_addr  Dirección I2C de 7 bits del esclavo.
 * @param data_rd   Buffer destino. No puede ser NULL.
 * @param size      Número de bytes a leer. Debe ser > 0.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_TIMEOUT, u otro error I2C.
 */
esp_err_t i2c_master_read_bytes(uint8_t dev_addr,
                                 uint8_t *data_rd,
                                 size_t size);

/**
 * @brief Transacción combinada write→read (repeated START).
 *        Requerida por ADS1115 para leer registros.
 *        Ref: ADS1115 Datasheet Section 9.5.3 — Read Operation.
 * @param dev_addr   Dirección I2C de 7 bits.
 * @param reg_data   Buffer con dirección de registro + datos a escribir.
 * @param write_size Bytes a escribir (incluye registro).
 * @param data_rd    Buffer para la respuesta leída.
 * @param read_size  Bytes a leer.
 * @return ESP_OK o código de error.
 */
esp_err_t i2c_master_write_read(uint8_t dev_addr,
                                 const uint8_t *reg_data, size_t write_size,
                                 uint8_t *data_rd,         size_t read_size);

/**
 * @brief Desinstala el driver I2C y libera recursos.
 * @return ESP_OK.
 */
esp_err_t i2c_master_deinit(void);

#endif /* I2C_DRIVER_H */