/* Perfil de compuesto (límites, ventanas de análisis, umbrales) y validación de cada comando de medida. */
#pragma once
#include "ps_common.h"

typedef struct {
    char name[16];
    int16_t e_min_mv, e_max_mv; uint16_t step_min_mv, step_max_mv, amp_max_mv;
    float freq_min_hz, freq_max_hz; uint16_t rate_min_mv_s, rate_max_mv_s;
    uint8_t gain_mask;            /* bit i = índice de ganancia i permitido (0..7) */
    uint8_t default_gain_idx, rload_idx; uint32_t r_ext_ohm, settle_us; uint16_t max_points;
    int8_t redox; ps_win_t peak_win, base_win1, base_win2;
    uint8_t sat_pct, small_pct, edge_pct, target_pct, fwhm_k;
    ps_params_t defaults;
} ps_profile_t;

void profile_default(ps_profile_t *p);
/* Coherencia del perfil frente a la física del equipo; en error, field = campo del struct. */
bool profile_check(const ps_profile_t *p, uint32_t dac_vref_mv, ps_err_t *err);
/* Un comando de medida debe pasar esto antes de ejecutarse; en error, field = nombre JSON del comando. */
bool profile_validate_run(const ps_profile_t *p, const ps_params_t *run, uint8_t gain_idx,
                          uint32_t dac_vref_mv, ps_err_t *err);
