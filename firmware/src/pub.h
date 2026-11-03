/* Tarea de publicación de baja prioridad: envía una pasada terminada como bloques JSON, solo entre pasadas. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ps_common.h"
#include "protocol.h"

/* Los punteros deben seguir válidos hasta que done_task reciba el aviso. */
typedef struct { const ps_run_meta_t *meta; const ps_sample_t *s; const float *i_na; size_t n;
                 const ps_features_t *f; const ps_adj_t *adj; size_t n_adj; uint32_t run_flags; bool send_result; } ps_pass_ctx_t;

esp_err_t pub_start(void);                                                  /* crea pub_task (prioridad 3, stack 6144) */
void      pub_publish_pass(const ps_pass_ctx_t *ctx, TaskHandle_t done_task);   /* encola; al terminar, xTaskNotifyGiveIndexed(done_task, 1) */
void      pub_status(const char *state, const ps_err_t *err);               /* retain para "idle", "running" y "offline"; seguro desde cualquier tarea */
