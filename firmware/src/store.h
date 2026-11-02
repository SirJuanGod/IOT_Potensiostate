/* Persistencia en NVS (namespace "ps"): perfiles de compuesto y calibración. */
#pragma once
#include "esp_err.h"
#include "ps_common.h"
#include "profile.h"

esp_err_t store_init(void);                              /* nvs_flash_init; si NO_FREE_PAGES o NEW_VERSION: erase + init */
esp_err_t store_get_active_profile(ps_profile_t *out);   /* perfil activo válido; si no hay o no pasa profile_check, profile_default */
esp_err_t store_put_profile(const ps_profile_t *p);      /* máx. 4; reemplaza por nombre; ESP_ERR_NO_MEM si ya hay 4 y es nuevo */
esp_err_t store_select_profile(const char *name);        /* ESP_ERR_NOT_FOUND si no existe */
esp_err_t store_get_cal(ps_cal_t *out);                  /* si no hay, ps_cal_default */
esp_err_t store_put_cal(const ps_cal_t *c);
