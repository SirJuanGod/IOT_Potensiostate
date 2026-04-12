#include <string.h>
#include "esp_log.h"
#include "spi_driver.h"
#include "app_config.h"

static const char *TAG = "SPI_DRIVER";
static spi_device_handle_t spi_handle;

esp_err_t spi_driver_init(void) {
    esp_err_t ret;
    
    spi_bus_config_t buscfg = {
        .miso_io_num = SPI_MISO_IO,
        .mosi_io_num = SPI_MOSI_IO,
        .sclk_io_num = SPI_SCLK_IO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = SPI_MAX_FREQ_HZ,
        .mode = 1,                                // Modo SPI 1 (CPOL=0, CPHA=1) requerido por AD5662
        .spics_io_num = SPI_CS_IO,
        .queue_size = 7,
    };

    ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando el bus SPI");
        return ret;
    }

    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &spi_handle);
    if (ret == ESP_OK) {
         ESP_LOGI(TAG, "SPI inicializado. CLK=%d, MOSI=%d, MISO=%d, CS=%d", 
                  SPI_SCLK_IO, SPI_MOSI_IO, SPI_MISO_IO, SPI_CS_IO);
    }
    
    return ret;
}

esp_err_t spi_driver_transfer_buffer(const uint8_t *data_out, uint8_t *data_in, size_t len_bytes) {
    if (len_bytes == 0) return ESP_OK;
    
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len_bytes * 8;
    t.tx_buffer = data_out;
    t.rx_buffer = data_in;

    return spi_device_transmit(spi_handle, &t);
}
