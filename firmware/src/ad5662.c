#include "ad5662.h"
#include <string.h>
#include "board.h"
#include "driver/spi_master.h"

static spi_device_handle_t s_dev;

esp_err_t ad5662_init(void)
{
    const spi_bus_config_t bus = {
        .mosi_io_num = PS_PIN_SPI_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PS_PIN_SPI_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4,
    };
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED);
    if (err != ESP_OK) return err;
    const spi_device_interface_config_t dev = {
        .mode = 1,
        .clock_speed_hz = PS_SPI_HZ,
        .spics_io_num = PS_PIN_SPI_CS,
        .queue_size = 1,
    };
    return spi_bus_add_device(SPI2_HOST, &dev, &s_dev);
}

esp_err_t ad5662_write(uint16_t code)
{
    spi_transaction_t t;
    memset(&t, 0, sizeof t);
    t.flags = SPI_TRANS_USE_TXDATA;
    t.length = 24;
    t.tx_data[0] = 0x00;   /* PD1:PD0 = 00, operación normal */
    t.tx_data[1] = (uint8_t)(code >> 8);
    t.tx_data[2] = (uint8_t)(code & 0xFF);
    return spi_device_polling_transmit(s_dev, &t);
}
