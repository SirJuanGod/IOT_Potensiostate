/**
 * @file i2c_driver.c
 * @brief Driver I2C maestro — ESP-IDF legacy driver (driver/i2c.h).
 *
 * NOTA IMPORTANTE PARA ESP-IDF v5.x:
 *   Este archivo usa el driver legacy (driver/i2c.h). Si el proyecto migra a
 *   ESP-IDF v5.4+ y otras librerías usan el nuevo driver (driver/i2c_master.h),
 *   los dos drivers NO pueden coexistir → migrar a i2c_new_master_bus().
 *   Ref: espressif/esp-idf issue #667 — driver_ng conflict check.
 *
 * NOTA DE PREFIJO:
 *   Se renombró de i2c_driver_* a i2c_master_* para evitar conflicto de
 *   símbolos con el namespace interno de ESP-IDF (i2c_driver_install,
 *   i2c_driver_delete). El archivo mantiene el nombre i2c_driver.c/.h.
 */

#include "i2c_driver.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "app_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "I2C_DRV";

/* [FIX] volatile — leída desde múltiples tareas (meas_task + init) */
static volatile bool s_initialized = false;


esp_err_t i2c_master_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "I2C ya inicializado, omitiendo re-init");
        return ESP_OK;
    }

    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = I2C_MASTER_SDA_IO,
        .scl_io_num       = I2C_MASTER_SCL_IO,
        /* Pull-ups internos ~45-100kΩ — INSUFICIENTES para 400kHz.
         * Habilitados solo como respaldo.
         * REQUIERE pull-ups externos 4.7kΩ en hardware para Fast Mode.
         * Ref: I2C Specification v6, Table 10 — Pull-up resistor values */
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    esp_err_t ret = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo en i2c_param_config: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2c_driver_install(I2C_MASTER_NUM, I2C_MODE_MASTER, 0, 0, 0);
    if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "I2C driver ya instalado en puerto %d", I2C_MASTER_NUM);
        s_initialized = true;
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo en i2c_driver_install: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "I2C maestro listo. SDA=GPIO%d SCL=GPIO%d @ %d Hz",
             I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO, I2C_MASTER_FREQ_HZ);
    return ESP_OK;
}


esp_err_t i2c_master_write_bytes(uint8_t dev_addr,
                                   const uint8_t *data_wr, size_t size)
{
    if (data_wr == NULL || size == 0) {
        ESP_LOGE(TAG, "write: data_wr NULL o size=0");
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        ESP_LOGE(TAG, "write: I2C no inicializado");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = i2c_master_write_to_device(I2C_MASTER_NUM, dev_addr,
                                                data_wr, size,
                                                pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "write→0x%02X falló: %s", dev_addr, esp_err_to_name(ret));
    }
    return ret;
}


esp_err_t i2c_master_read_bytes(uint8_t dev_addr,
                                  uint8_t *data_rd, size_t size)
{
    if (data_rd == NULL || size == 0) {
        ESP_LOGE(TAG, "read: data_rd NULL o size=0");
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        ESP_LOGE(TAG, "read: I2C no inicializado");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = i2c_master_read_from_device(I2C_MASTER_NUM, dev_addr,
                                                 data_rd, size,
                                                 pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "read←0x%02X falló: %s", dev_addr, esp_err_to_name(ret));
    }
    return ret;
}


esp_err_t i2c_master_write_read(uint8_t dev_addr,
                                  const uint8_t *reg_data, size_t write_size,
                                  uint8_t *data_rd,         size_t read_size)
{
    if (reg_data == NULL || write_size == 0 ||
        data_rd  == NULL || read_size  == 0) {
        ESP_LOGE(TAG, "write_read: argumentos inválidos");
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        ESP_LOGE(TAG, "write_read: I2C no inicializado");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = i2c_master_write_read_device(I2C_MASTER_NUM, dev_addr,
                                                   reg_data,  write_size,
                                                   data_rd,   read_size,
                                                   pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "write_read→0x%02X falló: %s",
                 dev_addr, esp_err_to_name(ret));
    }
    return ret;
}


esp_err_t i2c_master_deinit(void)
{
    if (!s_initialized) return ESP_OK;

    esp_err_t ret = i2c_driver_delete(I2C_MASTER_NUM);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al eliminar driver I2C: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialized = false;
    ESP_LOGI(TAG, "I2C driver liberado");
    return ESP_OK;
}