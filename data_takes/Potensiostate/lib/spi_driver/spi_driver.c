#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "spi_driver.h"
#include "app_config.h"

static const char *TAG = "SPI_DRIVER";
static spi_device_handle_t s_spi_handle = NULL;

WORD_ALIGNED_ATTR static uint8_t s_tx_buf[4];

esp_err_t spi_driver_init(void)
{
    esp_err_t ret;

    if (s_spi_handle != NULL) {
        ESP_LOGW(TAG, "SPI ya inicializado, omitiendo re-inicialización");
        return ESP_OK;
    }

    spi_bus_config_t buscfg = {
        .mosi_io_num     = SPI_MOSI_IO,
        .miso_io_num     = -1,
        .sclk_io_num     = SPI_SCLK_IO,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = ADVICE_FRAME_BYTES * 4,
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = SPI_MAX_FREQ_HZ,
        .mode           = 1,
        .spics_io_num   = SPI_CS_IO,
        .queue_size     = 4,
        .pre_cb         = NULL,
        .post_cb        = NULL,
    };

    ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al inicializar bus SPI: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &s_spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al agregar dispositivo: %s", esp_err_to_name(ret));
        spi_bus_free(SPI2_HOST);
        return ret;
    }

    ESP_LOGI(TAG, "Dispositivo SPI listo. CLK=%d Hz, MOSI=GPIO%d, CS=GPIO%d",
             SPI_MAX_FREQ_HZ, SPI_MOSI_IO, SPI_CS_IO);
    return ESP_OK;
}

esp_err_t spi_driver_send_advice(const uint8_t *data_out, uint32_t timeout_ms)
{
    if (data_out == NULL) {
        ESP_LOGE(TAG, "data_out es NULL");
        return ESP_ERR_INVALID_ARG;
    }
    if (s_spi_handle == NULL) {
        ESP_LOGE(TAG, "SPI no inicializado. Llamar spi_driver_init() primero");
        return ESP_ERR_INVALID_STATE;
    }

    memcpy(s_tx_buf, data_out, ADVICE_FRAME_BYTES);

    spi_transaction_t t = {
        .length    = ADVICE_FRAME_BYTES * 8,
        .tx_buffer = s_tx_buf,
        .rx_buffer = NULL,
        .flags     = 0,
    };

    esp_err_t ret = spi_device_queue_trans(s_spi_handle, &t,
                                           pdMS_TO_TICKS(timeout_ms > 0 ? timeout_ms : 5));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error encolando transacción SPI: %s", esp_err_to_name(ret));
        return ret;
    }

    spi_transaction_t *ret_trans = NULL;
    ret = spi_device_get_trans_result(s_spi_handle, &ret_trans,
                                      pdMS_TO_TICKS(timeout_ms > 0 ? timeout_ms : 5));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Timeout/error obteniendo resultado SPI: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t spi_driver_write_dac(uint16_t dac_value, uint8_t pd_mode, uint32_t timeout_ms)
{
    if (pd_mode > 0x03u) {
        ESP_LOGE(TAG, "pd_mode inválido (0x%02X). Usar AD5662_PD_* defines", pd_mode);
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t frame[ADVICE_FRAME_BYTES];
    frame[0] = (pd_mode & 0x03u);
    frame[1] = (uint8_t)(dac_value >> 8);
    frame[2] = (uint8_t)(dac_value & 0xFF);

    return spi_driver_send_advice(frame, timeout_ms);
}

esp_err_t spi_driver_deinit(void)
{
    if (s_spi_handle == NULL) {
        return ESP_OK;
    }

    esp_err_t ret = spi_bus_remove_device(s_spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al remover dispositivo SPI: %s", esp_err_to_name(ret));
        return ret;
    }
    s_spi_handle = NULL;

    ret = spi_bus_free(SPI2_HOST);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al liberar bus SPI: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SPI liberado correctamente");
    return ESP_OK;
}