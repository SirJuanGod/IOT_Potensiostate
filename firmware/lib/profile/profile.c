#include "profile.h"
#include <stdlib.h>
#include <string.h>
#include "potential.h"
#include "technique.h"

#define MAX_TIME_MS 3600000u   /* tope de cualquier tiempo del comando: 1 h */

static bool fail(ps_err_t *e, const char *field, int32_t limit, const char *msg) {
    ps_err_set(e, PS_ERR_RANGE, field, limit, msg);
    return false;
}

void profile_default(ps_profile_t *p) {
    memset(p, 0, sizeof *p);
    strcpy(p->name, "default");
    p->e_min_mv = -600; p->e_max_mv = 600;
    p->step_min_mv = 1; p->step_max_mv = 20; p->amp_max_mv = 100;
    p->freq_min_hz = 1.0f; p->freq_max_hz = 100.0f;
    p->rate_min_mv_s = 5; p->rate_max_mv_s = 500;
    p->gain_mask = 0x7F; p->default_gain_idx = 3; p->rload_idx = 0;
    p->r_ext_ohm = 0; p->settle_us = 500; p->max_points = 4000;
    p->redox = 1;
    p->peak_win = (ps_win_t){-600, 600};
    p->base_win1 = (ps_win_t){-600, -500};
    p->base_win2 = (ps_win_t){500, 600};
    p->sat_pct = 95; p->small_pct = 20; p->edge_pct = 10; p->target_pct = 80; p->fwhm_k = 3;
    p->defaults.tec = PS_TEC_SWV;
    p->defaults.p.swv = (ps_swv_t){-400, 500, 25, 2, 25.0f};
}

static bool win_ok(const ps_profile_t *p, ps_win_t w, const char *name, ps_err_t *err) {
    if (w.lo > w.hi || w.lo < p->e_min_mv || w.hi > p->e_max_mv)
        return fail(err, name, w.lo > w.hi ? w.lo : (w.lo < p->e_min_mv ? p->e_min_mv : p->e_max_mv),
                    "ventana desordenada o fuera de rango");
    return true;
}

bool profile_check(const ps_profile_t *p, uint32_t dac_vref_mv, ps_err_t *err) {
    int32_t emax_mv = potential_e_max_uv(dac_vref_mv) / 1000;
    if (p->e_min_mv >= p->e_max_mv) return fail(err, "e_max_mv", p->e_min_mv, "e_min debe ser menor que e_max");
    if (p->e_max_mv > emax_mv) return fail(err, "e_max_mv", emax_mv, "supera el rango fisico del equipo");
    if (p->e_min_mv < -emax_mv) return fail(err, "e_min_mv", -emax_mv, "supera el rango fisico del equipo");
    if (!win_ok(p, p->peak_win, "peak_win", err) || !win_ok(p, p->base_win1, "base_win1", err) ||
        !win_ok(p, p->base_win2, "base_win2", err)) return false;
    if (p->step_min_mv > p->step_max_mv) return fail(err, "step_min_mv", p->step_max_mv, "step_min > step_max");
    if (!(p->freq_min_hz > 0.0f)) return fail(err, "freq_min_hz", 0, "freq_min debe ser > 0");
    if (p->freq_min_hz > p->freq_max_hz) return fail(err, "freq_min_hz", (int32_t)p->freq_max_hz, "freq_min > freq_max");
    if (p->rate_min_mv_s == 0) return fail(err, "rate_min_mv_s", 0, "rate_min debe ser > 0");
    if (p->rate_min_mv_s > p->rate_max_mv_s) return fail(err, "rate_min_mv_s", p->rate_max_mv_s, "rate_min > rate_max");
    if (p->max_points > PS_MAX_SAMPLES) return fail(err, "max_points", PS_MAX_SAMPLES, "supera PS_MAX_SAMPLES");
    if (p->redox != 1 && p->redox != -1) return fail(err, "redox", 1, "redox debe ser +1 o -1");
    if (p->default_gain_idx > PS_GAIN_EXT || !(p->gain_mask >> p->default_gain_idx & 1))
        return fail(err, "default_gain_idx", p->gain_mask, "ganancia por defecto no permitida");
    return true;
}

/* potencial v dentro del perfil y de la física; field = nombre JSON del comando */
static bool e_ok(const ps_profile_t *p, int32_t v, int32_t emax_mv, const char *field, ps_err_t *err) {
    if (v < p->e_min_mv) return fail(err, field, p->e_min_mv, "fuera del perfil");
    if (v > p->e_max_mv) return fail(err, field, p->e_max_mv, "fuera del perfil");
    if (abs(v) > emax_mv) return fail(err, field, emax_mv, "fuera del rango fisico");
    return true;
}

static bool step_ok(const ps_profile_t *p, int32_t v, const char *field, ps_err_t *err) {
    v = abs(v);
    if (v < 1 || v < p->step_min_mv) return fail(err, field, p->step_min_mv ? p->step_min_mv : 1, "paso menor que el minimo");
    if (v > p->step_max_mv) return fail(err, field, p->step_max_mv, "paso mayor que el maximo");
    return true;
}

static bool ms_ok(uint32_t v, const char *field, ps_err_t *err) {
    return v <= MAX_TIME_MS || fail(err, field, MAX_TIME_MS, "tiempo demasiado largo");
}

/* tiempos y relaciones de los parámetros temporales (antes de calcular duraciones) */
static bool times_ok(const ps_params_t *r, ps_err_t *err) {
    switch (r->tec) {
    case PS_TEC_DPV: {
        const ps_dpv_t *d = &r->p.dpv;
        return ms_ok(d->period_ms, "period_ms", err) && ms_ok(d->width_ms, "width_ms", err) &&
               (d->width_ms <= d->period_ms || fail(err, "width_ms", (int32_t)d->period_ms, "width mayor que period"));
    }
    case PS_TEC_NPV: {
        const ps_npv_t *n = &r->p.npv;
        return ms_ok(n->quiet_ms, "quiet_ms", err) && ms_ok(n->period_ms, "period_ms", err) &&
               ms_ok(n->width_ms, "width_ms", err) &&
               (n->width_ms <= n->period_ms || fail(err, "width_ms", (int32_t)n->period_ms, "width mayor que period"));
    }
    case PS_TEC_CA: {
        const ps_ca_t *c = &r->p.ca;
        return ms_ok(c->quiet_ms, "quiet_ms", err) && ms_ok(c->t1_ms, "t1_ms", err) && ms_ok(c->t2_ms, "t2_ms", err);
    }
    default: return true;
    }
}

/* cada paso con muestra debe durar lo que necesita el hardware; las duraciones salen de los parámetros */
static bool sample_time_ok(const ps_profile_t *p, const ps_params_t *r, ps_err_t *err) {
    const uint32_t min_us = ps_min_sample_step_us(p->settle_us);
    uint32_t us, pulse_ms, base_ms;
    switch (r->tec) {
    case PS_TEC_SWV:
        us = (uint32_t)(1e6 / (2.0 * r->p.swv.freq_hz) + 0.5);   /* igual que tech_step_at */
        return us >= min_us || fail(err, "freq_hz", (int32_t)(1e6 / (2.0 * min_us)), "paso mas corto que el minimo del hardware");
    case PS_TEC_CV: {
        uint64_t num = (uint64_t)abs(r->p.cv.step) * 1000000u;
        us = (uint32_t)(num / r->p.cv.rate_mv_s);
        return us >= min_us || fail(err, "rate_mV_s", (int32_t)(num / min_us), "paso mas corto que el minimo del hardware");
    }
    case PS_TEC_DPV: pulse_ms = r->p.dpv.width_ms; base_ms = r->p.dpv.period_ms - r->p.dpv.width_ms; break;
    case PS_TEC_NPV: pulse_ms = r->p.npv.width_ms; base_ms = r->p.npv.period_ms - r->p.npv.width_ms; break;
    case PS_TEC_CA: {
        const ps_ca_t *c = &r->p.ca;
        uint32_t m = c->quiet_ms < c->t1_ms ? c->quiet_ms : c->t1_ms;
        if (c->t2_ms < m) m = c->t2_ms;
        us = m * 1000u / c->samples;
        return us >= min_us || fail(err, "samples", (int32_t)((uint64_t)m * 1000u / min_us), "paso mas corto que el minimo del hardware");
    }
    default: return true;
    }
    /* DPV/NPV: pulso y base se muestrean (en NPV el quiet no) */
    us = (pulse_ms < base_ms ? pulse_ms : base_ms) * 1000u;
    return us >= min_us || fail(err, "width_ms", (int32_t)((min_us + 999u) / 1000u), "paso mas corto que el minimo del hardware");
}

bool profile_validate_run(const ps_profile_t *p, const ps_params_t *run, uint8_t gain_idx,
                          uint32_t dac_vref_mv, ps_err_t *err) {
    const int32_t emax_mv = potential_e_max_uv(dac_vref_mv) / 1000;
    int16_t lo, hi;
    if (!times_ok(run, err)) return false;

    /* step / amp / freq / rate */
    switch (run->tec) {
    case PS_TEC_CV: {
        const ps_cv_t *c = &run->p.cv;
        if (c->cycles == 0) return fail(err, "cycles", 1, "se requiere al menos 1 ciclo");
        if (!step_ok(p, c->step, "step", err)) return false;
        if (c->rate_mv_s == 0 || c->rate_mv_s < p->rate_min_mv_s) return fail(err, "rate_mV_s", p->rate_min_mv_s, "velocidad menor que el minimo");
        if (c->rate_mv_s > p->rate_max_mv_s) return fail(err, "rate_mV_s", p->rate_max_mv_s, "velocidad mayor que el maximo");
        break;
    }
    case PS_TEC_SWV: {
        const ps_swv_t *s = &run->p.swv;
        if (!step_ok(p, s->step, "step", err)) return false;
        if (abs(s->amp) > p->amp_max_mv) return fail(err, "amp", p->amp_max_mv, "amplitud mayor que el maximo");
        if (!(s->freq_hz >= p->freq_min_hz)) return fail(err, "freq_hz", (int32_t)p->freq_min_hz, "frecuencia menor que el minimo");
        if (!(s->freq_hz <= p->freq_max_hz)) return fail(err, "freq_hz", (int32_t)p->freq_max_hz, "frecuencia mayor que el maximo");
        break;
    }
    case PS_TEC_DPV:
        if (!step_ok(p, run->p.dpv.step, "step", err)) return false;
        if (abs(run->p.dpv.amp) > p->amp_max_mv) return fail(err, "amp", p->amp_max_mv, "amplitud mayor que el maximo");
        break;
    case PS_TEC_NPV:   /* en NPV amp es el incremento de x y a la vez la amplitud: solo tope amp_max */
        if (abs(run->p.npv.amp) > p->amp_max_mv) return fail(err, "amp", p->amp_max_mv, "amplitud mayor que el maximo");
        break;
    case PS_TEC_CA:
        if (run->p.ca.samples == 0) return fail(err, "samples", 1, "se requiere al menos 1 muestra");
        break;
    }

    /* extremos: puntos nominales con su nombre; después el recorrido completo (±amp) */
    switch (run->tec) {
    case PS_TEC_CV: {
        const ps_cv_t *c = &run->p.cv;
        if (!e_ok(p, c->start, emax_mv, "start", err) || !e_ok(p, c->vertex1, emax_mv, "vertex1", err) ||
            !e_ok(p, c->vertex2, emax_mv, "vertex2", err) || !e_ok(p, c->end, emax_mv, "end", err)) return false;
        int32_t st = abs(c->step);   /* step_ok garantiza st >= 1 */
        if (abs(c->vertex1 - c->start) % st || abs(c->vertex2 - c->vertex1) % st || abs(c->end - c->vertex2) % st)
            return fail(err, "step", st, "el paso no divide los tramos del barrido");
        break;
    }
    case PS_TEC_SWV: case PS_TEC_DPV: case PS_TEC_NPV:
        if (!e_ok(p, run->tec == PS_TEC_SWV ? run->p.swv.start : run->tec == PS_TEC_DPV ? run->p.dpv.start : run->p.npv.start,
                  emax_mv, "start", err) ||
            !e_ok(p, run->tec == PS_TEC_SWV ? run->p.swv.end : run->tec == PS_TEC_DPV ? run->p.dpv.end : run->p.npv.end,
                  emax_mv, "end", err)) return false;
        break;
    case PS_TEC_CA:
        if (!e_ok(p, run->p.ca.pre_step, emax_mv, "pre_step", err) || !e_ok(p, run->p.ca.v1, emax_mv, "v1", err) ||
            !e_ok(p, run->p.ca.v2, emax_mv, "v2", err)) return false;
        break;
    }
    tech_e_extremes(run, &lo, &hi);
    if (!e_ok(p, lo, emax_mv, "amp", err) || !e_ok(p, hi, emax_mv, "amp", err)) return false;

    /* ganancia */
    if (gain_idx > PS_GAIN_EXT || !(p->gain_mask >> gain_idx & 1)) return fail(err, "gain", p->gain_mask, "ganancia no permitida");
    if (gain_idx == PS_GAIN_EXT && p->r_ext_ohm == 0) return fail(err, "gain", 0, "ganancia externa sin r_ext_ohm");

    /* puntos y tiempo por muestra */
    if (tech_num_samples(run) > p->max_points) return fail(err, "points", p->max_points, "demasiados puntos");
    return sample_time_ok(p, run, err);
}
