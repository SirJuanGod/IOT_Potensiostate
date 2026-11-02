/* Reglas fijas del ajuste automático en dos pasadas (exploración y pasada final), siempre dentro del perfil.
 * Sin dependencias de ESP-IDF. */
#pragma once
#include <stddef.h>
#include "ps_common.h"
#include "profile.h"

typedef struct { uint16_t max_abs_raw; bool has_peak; float peak_fs_frac; int16_t ep_mv; float fwhm_mv; bool at_edge; } autorange_input_t;
typedef struct { uint8_t gain_idx, pga_idx; } ps_gain_knobs_t;
typedef enum { PS_RULE_SATURATION, PS_RULE_SMALL, PS_RULE_EDGE, PS_RULE_ZOOM, PS_RULE_STEP_BUDGET } ps_rule_t;
typedef struct { ps_rule_t rule; int32_t b0, b1, a0, a1; } ps_adj_t;   /* antes/después; ganancia: b0=gain, b1=pga; ventana: lo, hi; presupuesto: b0=paso */

/* Parámetros de la pasada de exploración: ventana completa del perfil y paso grueso (CV y CA sin cambios). */
void   autorange_explore_params(const ps_params_t *req, const ps_profile_t *prof, ps_params_t *out);
/* Decide ganancias, ventana y paso de la pasada final; devuelve cuántos ajustes registró en adj (máx. max_adj). */
size_t autorange_decide(const autorange_input_t *in, const ps_profile_t *prof, const ps_params_t *req,
                        const ps_gain_knobs_t *cur, ps_params_t *final_p, ps_gain_knobs_t *final_k,
                        ps_adj_t *adj, size_t max_adj, bool *peak_at_edge);
