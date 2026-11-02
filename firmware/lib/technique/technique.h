/* Generador sin estado de la secuencia de pasos CV/SWV/DPV/NPV/CA (sin dependencias de ESP-IDF). */
#pragma once
#include "ps_common.h"

uint32_t tech_num_steps(const ps_params_t *p);
uint32_t tech_num_samples(const ps_params_t *p);
bool     tech_step_at(const ps_params_t *p, uint32_t k, ps_step_t *out);   /* false si k >= num_steps */
bool     tech_has_window(ps_tec_t tec);                                    /* true en SWV, DPV y NPV */
void     tech_get_window(const ps_params_t *p, int16_t *lo, int16_t *hi, int16_t *step); /* step NPV = amp */
void     tech_set_window(ps_params_t *p, int16_t lo, int16_t hi, int16_t step);         /* conserva el sentido start→end */
void     tech_e_extremes(const ps_params_t *p, int16_t *e_min, int16_t *e_max);        /* incluye ±amp y vértices */
