/* Pines, direcciones y frecuencias de la placa (ESP32-C3 SuperMini). */
#pragma once

#define PS_PIN_SPI_MOSI  5
#define PS_PIN_SPI_SCLK  4
#define PS_PIN_SPI_CS    6
#define PS_PIN_I2C_SDA   8
#define PS_PIN_I2C_SCL   9
#define PS_PIN_ADS_RDY   10   /* ALERT/RDY del ADS1115 */
#define PS_PIN_LMP_MENB  3    /* habilitación I2C del LMP91000, activo en bajo */

#define PS_I2C_ADDR_LMP  0x48
#define PS_I2C_ADDR_ADS  0x49
#define PS_I2C_HZ        400000
#define PS_SPI_HZ        10000000
#define PS_I2C_TMO_MS    10   /* timeout de toda transferencia I2C */
