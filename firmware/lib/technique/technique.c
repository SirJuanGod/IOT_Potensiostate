#include "technique.h"
#include <stdlib.h>

/* Aritmética pura: el paso k se calcula sin materializar la secuencia. */

static int32_t sgn(int32_t v) { return v >= 0 ? 1 : -1; }
/* magnitud de un paso/amplitud; mínimo 1 para no dividir por cero */
static int32_t mag1(int32_t v) { v = abs(v); return v ? v : 1; }
/* puntos de a a b con paso s, extremos incluidos (el último es el último que no se pasa de b) */
static uint32_t npts(int32_t a, int32_t b, int32_t s) { return (uint32_t)(abs(b - a) / s) + 1u; }
static uint32_t ms_us(uint32_t ms) { return ms * 1000u; }

/* ---- CV: ciclo = start→v1 (con start), v1∓step→v2, v2±step→end ---- */

typedef struct { uint32_t n1, n2, n3, len, skip; } cv_geo_t;

static cv_geo_t cv_geo(const ps_cv_t *c) {
    cv_geo_t g; int32_t st = mag1(c->step);
    g.n1 = npts(c->start, c->vertex1, st);
    g.n2 = (uint32_t)(abs(c->vertex2 - c->vertex1) / st);
    g.n3 = (uint32_t)(abs(c->end - c->vertex2) / st);
    g.len = g.n1 + g.n2 + g.n3;
    g.skip = (c->end == c->start) ? 1u : 0u;   /* sin punto duplicado entre ciclos */
    return g;
}

static uint32_t cv_steps(const ps_cv_t *c) {
    if (c->cycles == 0) return 0;
    cv_geo_t g = cv_geo(c);
    return g.len + (uint32_t)(c->cycles - 1) * (g.len - g.skip);
}

static void cv_at(const ps_cv_t *c, uint32_t k, ps_step_t *o) {
    cv_geo_t g = cv_geo(c); int32_t st = mag1(c->step), e, d;
    uint32_t j = k, cyc = 0;
    if (k >= g.len) {
        uint32_t per = g.len - g.skip;
        k -= g.len; cyc = 1 + k / per; j = k % per + g.skip;
    }
    if (j < g.n1) {
        d = c->vertex1 > c->start ? 1 : -1;
        e = c->start + d * (int32_t)j * st;
    } else if (j < g.n1 + g.n2) {
        d = sgn(c->vertex2 - c->vertex1);
        e = c->vertex1 + d * (int32_t)(j - g.n1 + 1) * st;
    } else {
        d = sgn(c->end - c->vertex2);
        e = c->vertex2 + d * (int32_t)(j - g.n1 - g.n2 + 1) * st;
    }
    o->e_mv = o->x_mv = (int16_t)e;
    o->dur_us = (uint32_t)((uint64_t)st * 1000000u / (c->rate_mv_s ? c->rate_mv_s : 1u));
    o->tag = PS_TAG_STEP; o->cycle = (uint8_t)cyc; o->branch = d > 0 ? PS_BR_UP : PS_BR_DOWN; o->sample = true;
}

/* ---- Técnicas de pulsos: x recorre start→end ---- */

static void fill(ps_step_t *o, int32_t e, int32_t x, uint32_t dur, uint8_t tag, bool sample) {
    o->e_mv = (int16_t)e; o->x_mv = (int16_t)x; o->dur_us = dur; o->tag = tag;
    o->cycle = 0; o->branch = PS_BR_UP; o->sample = sample;
}

/* SWV y DPV: el pulso (fwd en SWV) va en el sentido del barrido, así la diferencia tiene el signo de redox */
static void swv_at(const ps_swv_t *s, uint32_t k, ps_step_t *o) {
    int32_t d = sgn(s->end - s->start), x = s->start + d * (int32_t)(k / 2) * mag1(s->step), a = d * abs(s->amp);
    double half = 1e6 / (2.0 * s->freq_hz);
    fill(o, (k & 1) ? x - a : x + a, x, (uint32_t)(half + 0.5), (k & 1) ? PS_TAG_REV : PS_TAG_FWD, true);
}

static void dpv_at(const ps_dpv_t *d, uint32_t k, ps_step_t *o) {
    int32_t dir = sgn(d->end - d->start), x = d->start + dir * (int32_t)(k / 2) * mag1(d->step);
    uint32_t base = d->period_ms > d->width_ms ? d->period_ms - d->width_ms : 0;
    if (k & 1) fill(o, x + dir * abs(d->amp), x, ms_us(d->width_ms), PS_TAG_PULSE, true);
    else       fill(o, x, x, ms_us(base), PS_TAG_BASE, true);
}

static void npv_at(const ps_npv_t *n, uint32_t k, ps_step_t *o) {
    uint32_t base = n->period_ms > n->width_ms ? n->period_ms - n->width_ms : 0;
    if (k == 0) { fill(o, n->start, n->start, ms_us(n->quiet_ms), PS_TAG_QUIET, false); return; }
    k--;
    int32_t x = n->start + sgn(n->end - n->start) * (int32_t)(k / 2) * mag1(n->amp);
    if (k & 1) fill(o, n->start, x, ms_us(base), PS_TAG_BASE, true);
    else       fill(o, x, x, ms_us(n->width_ms), PS_TAG_PULSE, true);
}

static void ca_at(const ps_ca_t *c, uint32_t k, ps_step_t *o) {
    uint32_t seg = k / c->samples;
    int32_t e = seg == 0 ? c->pre_step : seg == 1 ? c->v1 : c->v2;
    uint32_t t_ms = seg == 0 ? c->quiet_ms : seg == 1 ? c->t1_ms : c->t2_ms;
    fill(o, e, e, ms_us(t_ms) / c->samples, PS_TAG_STEP, true);
    o->branch = (uint8_t)seg;
}

/* ---- API ---- */

uint32_t tech_num_steps(const ps_params_t *p) {
    switch (p->tec) {
    case PS_TEC_CV:  return cv_steps(&p->p.cv);
    case PS_TEC_SWV: return 2u * npts(p->p.swv.start, p->p.swv.end, mag1(p->p.swv.step));
    case PS_TEC_DPV: return 2u * npts(p->p.dpv.start, p->p.dpv.end, mag1(p->p.dpv.step));
    case PS_TEC_NPV: return 1u + 2u * npts(p->p.npv.start, p->p.npv.end, mag1(p->p.npv.amp));
    case PS_TEC_CA:  return 3u * p->p.ca.samples;
    }
    return 0;
}

uint32_t tech_num_samples(const ps_params_t *p) {
    uint32_t n = tech_num_steps(p);
    return p->tec == PS_TEC_NPV ? n - 1u : n;   /* el paso QUIET de NPV no se muestrea */
}

bool tech_step_at(const ps_params_t *p, uint32_t k, ps_step_t *out) {
    if (k >= tech_num_steps(p)) return false;
    switch (p->tec) {
    case PS_TEC_CV:  cv_at(&p->p.cv, k, out); break;
    case PS_TEC_SWV: swv_at(&p->p.swv, k, out); break;
    case PS_TEC_DPV: dpv_at(&p->p.dpv, k, out); break;
    case PS_TEC_NPV: npv_at(&p->p.npv, k, out); break;
    case PS_TEC_CA:  ca_at(&p->p.ca, k, out); break;
    }
    return true;
}

bool tech_has_window(ps_tec_t tec) { return tec == PS_TEC_SWV || tec == PS_TEC_DPV || tec == PS_TEC_NPV; }

/* punteros a start/end/step de las técnicas con ventana (en NPV el "paso" es amp) */
static bool win_ptrs(ps_params_t *p, int16_t **s, int16_t **e, int16_t **st) {
    switch (p->tec) {
    case PS_TEC_SWV: *s = &p->p.swv.start; *e = &p->p.swv.end; *st = &p->p.swv.step; return true;
    case PS_TEC_DPV: *s = &p->p.dpv.start; *e = &p->p.dpv.end; *st = &p->p.dpv.step; return true;
    case PS_TEC_NPV: *s = &p->p.npv.start; *e = &p->p.npv.end; *st = &p->p.npv.amp;  return true;
    default: return false;
    }
}

void tech_get_window(const ps_params_t *p, int16_t *lo, int16_t *hi, int16_t *step) {
    int16_t *s, *e, *st;
    if (!win_ptrs((ps_params_t *)p, &s, &e, &st)) return;   /* solo lectura */
    *lo = *s < *e ? *s : *e; *hi = *s < *e ? *e : *s; *step = *st;
}

void tech_set_window(ps_params_t *p, int16_t lo, int16_t hi, int16_t step) {
    int16_t *s, *e, *st;
    if (!win_ptrs(p, &s, &e, &st)) return;
    if (*s > *e) { *s = hi; *e = lo; } else { *s = lo; *e = hi; }
    *st = step;
}

void tech_e_extremes(const ps_params_t *p, int16_t *e_min, int16_t *e_max) {
    int32_t lo, hi, up = 0, dn = 0;   /* margen hacia abajo / arriba sobre el rango de x */
    switch (p->tec) {
    case PS_TEC_CV: {
        const ps_cv_t *c = &p->p.cv;
        lo = hi = c->start;
        int32_t v[3] = { c->vertex1, c->vertex2, c->end };
        for (int i = 0; i < 3; i++) { if (v[i] < lo) lo = v[i]; if (v[i] > hi) hi = v[i]; }
        break; }
    case PS_TEC_CA: {
        const ps_ca_t *c = &p->p.ca;
        lo = hi = c->pre_step;
        int32_t v[2] = { c->v1, c->v2 };
        for (int i = 0; i < 2; i++) { if (v[i] < lo) lo = v[i]; if (v[i] > hi) hi = v[i]; }
        break; }
    default: {
        int16_t l, h, st;
        tech_get_window(p, &l, &h, &st); lo = l; hi = h;
        /* SWV aplica x±amp; DPV x+amp en el sentido del barrido; NPV aplica x y start (dentro del rango) */
        if (p->tec == PS_TEC_SWV) dn = up = abs(p->p.swv.amp);
        else if (p->tec == PS_TEC_DPV) *(p->p.dpv.end >= p->p.dpv.start ? &up : &dn) = abs(p->p.dpv.amp);
        break; }
    }
    *e_min = (int16_t)(lo - dn); *e_max = (int16_t)(hi + up);
}
