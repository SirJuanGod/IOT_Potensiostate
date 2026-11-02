/* Driver del AFE LMP91000 (I2C 0x48). Sin ESP_ERROR_CHECK: todo devuelve esp_err_t. */
#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

esp_err_t lmp_init(i2c_master_bus_handle_t bus);               /* MENB bajo; espera STATUS listo (solo en el arranque) */
esp_err_t lmp_configure(uint8_t gain_idx, uint8_t rload_idx);  /* desbloquea, TIACN, REFCN(+,0), MODECN 3 terminales; queda desbloqueado */
esp_err_t lmp_set_bias(int8_t sign, uint8_t bias_idx);         /* escribe REFCN solo si cambió */
esp_err_t lmp_lock(void);
