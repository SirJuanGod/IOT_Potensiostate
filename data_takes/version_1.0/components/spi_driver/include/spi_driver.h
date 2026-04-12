#ifndef SPI_DRIVER_H
#define SPI_DRIVER_H

#include "esp_err.h"
#include "driver/spi_master.h"

/**
 * @brief Inicializa el bus SPI usando los pines de app_config.h
 */
esp_err_t spi_driver_init(void);

/**
 * @brief Transfiere un buffer de datos por SPI (AD5662 DAC)
 * @param data_out Puntero a datos a enviar (debe estar en memoria DMA-capable)
 * @param data_in Puntero a buffer para recibir (puede ser NULL)
 * @param len_bytes Cantidad de bytes a transferir
 */
esp_err_t spi_driver_transfer_buffer(const uint8_t *data_out, uint8_t *data_in, size_t len_bytes);

#endif // SPI_DRIVER_H
