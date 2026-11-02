/* Gancho para la etapa 2: características del voltamograma -> concentración (modelo de IA). */
#pragma once
#include <stdbool.h>
#include "ps_common.h"
#include "analysis.h"

/* Devuelve true y escribe conc si hay un modelo; por ahora no hay ninguno. */
bool model_predict(const ps_features_t *f, const ps_params_t *p, float *conc);
