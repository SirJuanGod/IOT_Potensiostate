#include "autorange.h"
#include <math.h>
#include <stdlib.h>
#include "technique.h"

#define PGA_MAX 5

/* límites de la ventana de x para la técnica: el recorrido real suma ±amp (SWV) o +amp (DPV) */
static void win_limits(const ps_params_t *p, const ps_profile_t *prof, int32_t *lo, int32_t *hi) {
    *lo = prof->e_min_mv; *hi = prof->e_max_mv;
    if (p->tec == PS_TEC_SWV) { *lo += abs(p->p.swv.amp); *hi -= abs(p->p.swv.amp); }
    else if (p->tec == PS_TEC_DPV) *hi -= abs(p->p.dpv.amp);
}

/* tope del "paso": en NPV es amp y se valida contra amp_max */
static int32_t step_cap(const ps_params_t *p, const ps_profile_t *prof) {
    return p->tec == PS_TEC_NPV ? prof->amp_max_mv : prof->step_max_mv;
}

/* ventana de exploración: la del perfil; en NPV se conserva start y end va al límite en su sentido */
static void explore_window(const ps_params_t *req, const ps_profile_t *prof, int32_t *lo, int32_t *hi) {
    win_limits(req, prof, lo, hi);
    if (req->tec == PS_TEC_NPV) {
        if (req->p.npv.end >= req->p.npv.start) *lo = req->p.npv.start; else *hi = req->p.npv.start;
    }
}

void autorange_explore_params(const ps_params_t *req, const ps_profile_t *prof, ps_params_t *out) {
    *out = *req;
    if (!tech_has_window(req->tec)) return;
    int16_t l, h, st;
    int32_t lo, hi, s, cap = step_cap(req, prof);
    tech_get_window(req, &l, &h, &st);
    explore_window(req, prof, &lo, &hi);
    s = 4 * abs(st);
    tech_set_window(out, (int16_t)lo, (int16_t)hi, (int16_t)(s > cap ? cap : s));
}

/* siguiente ganancia interna permitida en el sentido dir; -1 si no hay */
static int next_gain(uint8_t mask, int g, int dir) {
    for (g += dir; g >= 0 && g < 7; g += dir) if (mask >> g & 1) return g;
    return -1;
}

static void record(ps_adj_t *adj, size_t max_adj, size_t *n, ps_rule_t r, int32_t b0, int32_t b1, int32_t a0, int32_t a1) {
    if (!adj || *n >= max_adj) return;
    adj[*n] = (ps_adj_t){r, b0, b1, a0, a1};
    (*n)++;
}

size_t autorange_decide(const autorange_input_t *in, const ps_profile_t *prof, const ps_params_t *req,
                        const ps_gain_knobs_t *cur, ps_params_t *final_p, ps_gain_knobs_t *final_k,
                        ps_adj_t *adj, size_t max_adj, bool *peak_at_edge) {
    size_t n = 0;
    ps_gain_knobs_t k = *cur, before;
    const float frac = in->max_abs_raw / 32767.0f;
    *final_p = *req;
    if (peak_at_edge) *peak_at_edge = false;

    /* 1. saturación: menos sensibilidad (PGA primero, luego ganancia); con ganancia externa solo la PGA */
    if (frac * 100.0f > prof->sat_pct) {
        before = k;
        if (k.pga_idx > 0) k.pga_idx--;
        else if (k.gain_idx != PS_GAIN_EXT) {
            int g = next_gain(prof->gain_mask, k.gain_idx, -1);
            if (g >= 0) k.gain_idx = (uint8_t)g;
        }
        if (k.gain_idx != before.gain_idx || k.pga_idx != before.pga_idx)
            record(adj, max_adj, &n, PS_RULE_SATURATION, before.gain_idx, before.pga_idx, k.gain_idx, k.pga_idx);
    }

    /* 2. señal pequeña: sube PGA y luego ganancia mientras la proyección de la señal máxima no pase del objetivo */
    if (in->has_peak && in->peak_fs_frac * 100.0f < prof->small_pct) {
        before = k;
        for (;;) {
            ps_gain_knobs_t t = k;
            if (t.pga_idx < PGA_MAX) t.pga_idx++;
            else if (t.gain_idx != PS_GAIN_EXT) {
                int g = next_gain(prof->gain_mask, t.gain_idx, +1);
                if (g < 0) break;
                t.gain_idx = (uint8_t)g;
            } else break;
            float proj = frac * ((float)PS_PGA_FSR_MV[cur->pga_idx] / PS_PGA_FSR_MV[t.pga_idx]);
            if (t.gain_idx != cur->gain_idx) proj *= (float)PS_TIA_GAIN_OHM[t.gain_idx] / PS_TIA_GAIN_OHM[cur->gain_idx];
            if (proj * 100.0f > prof->target_pct) break;
            k = t;
        }
        if (k.gain_idx != before.gain_idx || k.pga_idx != before.pga_idx)
            record(adj, max_adj, &n, PS_RULE_SMALL, before.gain_idx, before.pga_idx, k.gain_idx, k.pga_idx);
    }
    *final_k = k;

    if (!tech_has_window(req->tec)) return n;

    /* 3. ventana: sin pico se mantiene lo pedido; en el borde, la explorada; si no, zoom al pico */
    int16_t l0, h0, st;
    int32_t lo, hi;
    tech_get_window(req, &l0, &h0, &st);
    if (in->has_peak && in->at_edge) {
        explore_window(req, prof, &lo, &hi);
        if (peak_at_edge) *peak_at_edge = true;
    } else if (in->has_peak) {
        int32_t lim_lo, lim_hi;
        float half = (float)prof->fwhm_k * in->fwhm_mv;
        lo = (int32_t)floorf(in->ep_mv - half); hi = (int32_t)ceilf(in->ep_mv + half);
        const ps_win_t *b[2] = {&prof->base_win1, &prof->base_win2};
        for (int i = 0; i < 2; i++) { if (b[i]->lo < lo) lo = b[i]->lo; if (b[i]->hi > hi) hi = b[i]->hi; }
        win_limits(req, prof, &lim_lo, &lim_hi);
        if (lo < lim_lo) lo = lim_lo;
        if (hi > lim_hi) hi = lim_hi;
    } else {
        lo = l0; hi = h0;
    }
    if (lo <= hi && (lo != l0 || hi != h0)) {
        tech_set_window(final_p, (int16_t)lo, (int16_t)hi, st);
        record(adj, max_adj, &n, in->at_edge && in->has_peak ? PS_RULE_EDGE : PS_RULE_ZOOM, l0, h0, lo, hi);
    }

    /* 4. presupuesto de puntos: el menor paso que cabe (sin pasar del máximo del perfil) */
    if (tech_num_samples(final_p) > prof->max_points) {
        int32_t s = abs(st) ? abs(st) : 1, old = s, cap = step_cap(req, prof);
        tech_get_window(final_p, &l0, &h0, &st);
        while (s < cap) {
            tech_set_window(final_p, l0, h0, (int16_t)++s);
            if (tech_num_samples(final_p) <= prof->max_points) break;
        }
        if (s != old) record(adj, max_adj, &n, PS_RULE_STEP_BUDGET, old, 0, s, 0);
    }
    return n;
}
