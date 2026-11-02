/* Motor de medición: meas_task ejecuta los pasos con esp_timer one-shot y la interrupción RDY del ADS1115. */
#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "protocol.h"

/* Bits de notificación de meas_task, índice 0 (el índice 1 es "publicación terminada" de pub_task). */
#define EVT_RUN         (1u << 0)
#define EVT_STEP_END    (1u << 1)
#define EVT_ADC_TRIGGER (1u << 2)
#define EVT_ADC_RDY     (1u << 3)
#define EVT_STOP        (1u << 4)

esp_err_t    engine_start(void);                  /* crea meas_task (prioridad 10, stack 6144) y los dos esp_timer */
TaskHandle_t engine_task(void);                   /* handle de meas_task (para ads_init); NULL antes de engine_start */
esp_err_t    engine_submit(const ps_cmd_t *run);  /* ESP_ERR_INVALID_STATE si hay un run en cola o en curso */
void         engine_request_stop(void);           /* xTaskNotify(meas_task, EVT_STOP, eSetBits) */
bool         engine_busy(void);
esp_err_t    engine_safe_state(void);             /* bias 0 %, VREF = PS_VREF_ZERO_MV, LMP bloqueado */
