#ifndef I2C_DRIVER_H
#define I2C_DRIVER_H

#include "esp_err.h"
#include <stdint.h>

/**
 * @brief Inicializa el bus I2C maestro usando los pines definidos en app_config.h
 * 
 * @return esp_err_t ESP_OK si fue exitoso
 */
esp_err_t i2c_driver_init(void);

/**
 * @brief Función auxiliar para escribir datos en un dispositivo I2C
 */
esp_err_t i2c_driver_write(uint8_t dev_addr, uint8_t *data_wr, size_t size);

/**
 * @brief Función auxiliar para leer datos de un dispositivo I2C
 */
esp_err_t i2c_driver_read(uint8_t dev_addr, uint8_t *data_rd, size_t size);

#endif // I2C_DRIVER_H
