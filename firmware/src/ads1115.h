/* Driver del ADC ADS1115 (I2C 0x49), diferencial AIN0-AIN1, aviso de conversión lista por ALERT/RDY (GPIO10). */
#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Hi_thresh=0x8000, Lo_thresh=0x0000 (modo RDY); ISR en flanco de bajada -> xTaskNotifyFromISR(notify, notify_bit, eSetBits) */
esp_err_t ads_init(i2c_master_bus_handle_t bus, TaskHandle_t notify, uint32_t notify_bit);
esp_err_t ads_start(uint8_t pga_idx, uint8_t dr_idx);   /* lanza una conversión single-shot */
esp_err_t ads_read(int16_t *raw);
