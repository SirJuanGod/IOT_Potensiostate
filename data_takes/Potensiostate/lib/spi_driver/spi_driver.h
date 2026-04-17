#ifndef SPI_DRIVER_H
#define SPI_DRIVER_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "driver/spi_master.h"

/* =========================================================
 * AD5662 — Trama de 24 bits (DB23..DB0)
 *   [23:18] Don't care (6 bits)
 *   [17:16] PD1:PD0 → 0b00 = Normal Operation
 *   [15:0]  Dato DAC (16 bits, MSB primero)
 *
 * SPI: CPOL=0, CPHA=1 (Modo 1) — captura en flanco de bajada
 * ========================================================= */

#define ADVICE_FRAME_BYTES      3u      /**< Trama obligatoria: 24 bits = 3 bytes        */
#define ADVICE_PD_NORMAL        0x00u   /**< PD1=0, PD0=0 → Normal Operation            */
#define ADVICE_PD_1K            0x01u   /**< PD1=0, PD0=1 → Power-Down 1kΩ a GND       */
#define ADVICE_PD_100K          0x02u   /**< PD1=1, PD0=0 → Power-Down 100kΩ a GND     */
#define ADVICE_PD_TRISTATE      0x03u   /**< PD1=1, PD0=1 → Power-Down Hi-Z            */

esp_err_t spi_driver_init(void);
esp_err_t spi_driver_send_advice(const uint8_t *data_out, uint32_t timeout_ms);
esp_err_t spi_driver_write_dac(uint16_t dac_value, uint8_t pd_mode, uint32_t timeout_ms);
esp_err_t spi_driver_deinit(void);

#endif