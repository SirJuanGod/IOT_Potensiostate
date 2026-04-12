#include <stdio.h>
#include "esp_log.h"
#include "driver/i2c.h"
#include "i2c_driver.h"
#include "app_config.h"

static const char *TAG = "I2C_DRIVER";

esp_err_t i2c_driver_init(void) {
    int i2c_master_port = I2C_MASTER_NUM;

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    esp_err_t ret = i2c_param_config(i2c_master_port, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error en i2c_param_config");
        return ret;
    }

    ret = i2c_driver_install(i2c_master_port, conf.mode, 0, 0, 0);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "I2C master configurado en SDA=%d SCL=%d", I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
    } else {
        ESP_LOGE(TAG, "Error inicializando I2C master");
    }
    return ret;
}

esp_err_t i2c_driver_write(uint8_t dev_addr, uint8_t *data_wr, size_t size) {
    return i2c_master_write_to_device(I2C_MASTER_NUM, dev_addr, data_wr, size, I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
}

esp_err_t i2c_driver_read(uint8_t dev_addr, uint8_t *data_rd, size_t size) {
    return i2c_master_read_from_device(I2C_MASTER_NUM, dev_addr, data_rd, size, I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
}
