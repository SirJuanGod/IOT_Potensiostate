#include "potential.h"

/* Toda la aritmética es entera en µV (el C3 no tiene FPU). */

uint32_t potential_vref_max_uv(uint32_t dac_vref_mv) {
    uint64_t m = (uint64_t)dac_vref_mv * 1000u * 65535u / 65536u;
    uint64_t lim = (uint64_t)PS_VREF_MAX_MV * 1000u;
    return (uint32_t)(m < lim ? m : lim);
}

int32_t potential_e_max_uv(uint32_t dac_vref_mv) {
    return (int32_t)((uint64_t)potential_vref_max_uv(dac_vref_mv) * PS_BIAS_PCT[PS_NUM_BIAS - 1] / 100u);
}

/* VREF real que entrega el DAC para un código */
static uint64_t vref_of(uint32_t code, uint64_t dac_uv) { return (uint64_t)code * dac_uv / 65536u; }

/* código = round(vref·65536/dac_uv), saturado a 16 bits */
static uint32_t code_of(uint64_t vref_uv, uint64_t dac_uv) {
    uint64_t c = (vref_uv * 65536u + dac_uv / 2u) / dac_uv;
    return (uint32_t)(c > 65535u ? 65535u : c);
}

bool potential_solve(int32_t e_mv, uint32_t dac_vref_mv, ps_bias_t *out) {
    if (dac_vref_mv == 0) return false;
    const uint64_t dac_uv = (uint64_t)dac_vref_mv * 1000u;
    const uint64_t vmin = (uint64_t)PS_VREF_MIN_MV * 1000u, vmax = potential_vref_max_uv(dac_vref_mv);
    int64_t e_uv = (int64_t)e_mv * 1000;
    uint64_t a = (uint64_t)(e_uv < 0 ? -e_uv : e_uv);

    out->sign = e_uv < 0 ? -1 : 1;
    out->snapped = false;
    if (a > 0 && a < 15000u) {            /* zona de snap: 0 mV (< 7,5 mV) o ±15 mV */
        out->snapped = true;
        a = a < 7500u ? 0 : 15000u;
    }
    if (a == 0) {                         /* E = 0: bias 0, VREF fijo */
        out->bias_idx = 0; out->e_apl_uv = 0; out->sign = 1;
        out->dac_code = (uint16_t)code_of((uint64_t)PS_VREF_ZERO_MV * 1000u, dac_uv);
        return true;
    }
    if (a > (uint64_t)potential_e_max_uv(dac_vref_mv)) return false;

    bool found = false; uint64_t best_err = 0; uint32_t best_code = 0; uint8_t best_idx = 0; uint64_t best_e = 0;
    for (uint8_t idx = 1; idx < PS_NUM_BIAS; idx++) {
        uint32_t pct = PS_BIAS_PCT[idx];
        uint64_t vref = (a * 100u + pct / 2u) / pct;
        if (vref < vmin || vref > vmax) continue;
        uint32_t code = code_of(vref, dac_uv);
        while (code < 65535u && vref_of(code, dac_uv) < vmin) code++;   /* el redondeo puede salirse del rango */
        while (code > 0 && vref_of(code, dac_uv) > vmax) code--;
        uint64_t e_apl = vref_of(code, dac_uv) * pct / 100u;
        uint64_t err = e_apl > a ? e_apl - a : a - e_apl;
        if (!found || err < best_err || (err == best_err && code > best_code)) {
            found = true; best_err = err; best_code = code; best_idx = idx; best_e = e_apl;
        }
    }
    if (!found) return false;
    out->bias_idx = best_idx; out->dac_code = (uint16_t)best_code;
    out->e_apl_uv = (int32_t)(e_uv < 0 ? -(int64_t)best_e : (int64_t)best_e);
    return true;
}
