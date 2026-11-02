/* Driver del DAC AD5662 (SPI2, modo 1, trama de 24 bits); entrega el VREF externo del LMP91000. */
#pragma once
#include <stdint.h>
#include "esp_err.h"

esp_err_t ad5662_init(void);
esp_err_t ad5662_write(uint16_t code);   /* spi_device_polling_transmit */
