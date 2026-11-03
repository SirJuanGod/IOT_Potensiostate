#include "protocol.h"
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"

static const char *const TEC_NAME[] = {"cv", "swv", "dpv", "npv", "ca"};
#define NUM_TEC 5
#define ID_MAX 31

/* ---------------------------------------------------------------- parseo */

static bool bad(ps_err_t *e, ps_err_code_t code, const char *field, int32_t limit, const char *msg) {
    ps_err_set(e, code, field, limit, msg);
    return false;
}

/* Entero obligatorio dentro de [lo, hi]: no número o no entero -> TYPE; fuera de rango -> RANGE (nunca se trunca). */
static bool get_i(const cJSON *o, const char *key, long long lo, long long hi, long long *out, ps_err_t *err) {
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!it) return bad(err, PS_ERR_MISSING, key, 0, "falta el campo");
    if (!cJSON_IsNumber(it)) return bad(err, PS_ERR_TYPE, key, 0, "debe ser un numero");
    double d = it->valuedouble;
    if (d != floor(d)) return bad(err, PS_ERR_TYPE, key, 0, "debe ser entero");
    if (d < (double)lo || d > (double)hi) return bad(err, PS_ERR_RANGE, key, (int32_t)(d < (double)lo ? lo : hi), "fuera del rango del tipo");
    *out = (long long)d;
    return true;
}
static bool num_f(const cJSON *it, const char *key, float *out, ps_err_t *err) {
    if (!cJSON_IsNumber(it)) return bad(err, PS_ERR_TYPE, key, 0, "debe ser un numero");
    if (fabs(it->valuedouble) > (double)FLT_MAX) return bad(err, PS_ERR_RANGE, key, 0, "fuera del rango de float");
    *out = (float)it->valuedouble;
    return true;
}
static bool get_f(const cJSON *o, const char *key, float *out, ps_err_t *err) {
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!it) return bad(err, PS_ERR_MISSING, key, 0, "falta el campo");
    return num_f(it, key, out, err);
}
/* Cadena obligatoria de 1..max caracteres. */
static bool get_s(const cJSON *o, const char *key, size_t max, char *dst, ps_err_t *err) {
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!it) return bad(err, PS_ERR_MISSING, key, 0, "falta el campo");
    if (!cJSON_IsString(it)) return bad(err, PS_ERR_TYPE, key, 0, "debe ser texto");
    size_t n = strlen(it->valuestring);
    if (n < 1 || n > max) return bad(err, PS_ERR_RANGE, key, (int32_t)max, "longitud fuera de rango");
    memcpy(dst, it->valuestring, n + 1);
    return true;
}
static bool get_obj(const cJSON *o, const char *key, const cJSON **out, ps_err_t *err) {
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!it) return bad(err, PS_ERR_MISSING, key, 0, "falta el campo");
    if (!cJSON_IsObject(it)) return bad(err, PS_ERR_TYPE, key, 0, "debe ser un objeto");
    *out = it;
    return true;
}

#define GI(o, key, dst, lo, hi) do { long long v_; if (!get_i(o, key, lo, hi, &v_, err)) return false; dst = (__typeof__(dst))v_; } while (0)
#define I16(o, key, dst) GI(o, key, dst, INT16_MIN, INT16_MAX)
#define U16(o, key, dst) GI(o, key, dst, 0, UINT16_MAX)
#define U8(o, key, dst)  GI(o, key, dst, 0, UINT8_MAX)
#define U32(o, key, dst) GI(o, key, dst, 0, UINT32_MAX)

static bool get_tec(const cJSON *o, ps_tec_t *tec, ps_err_t *err) {
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(o, "tec");
    if (!it) return bad(err, PS_ERR_MISSING, "tec", 0, "falta el campo");
    if (!cJSON_IsString(it)) return bad(err, PS_ERR_TYPE, "tec", 0, "debe ser texto");
    for (int i = 0; i < NUM_TEC; i++)
        if (!strcmp(it->valuestring, TEC_NAME[i])) { *tec = (ps_tec_t)i; return true; }
    return bad(err, PS_ERR_RANGE, "tec", 0, "tecnica desconocida");
}

static bool parse_params(const cJSON *o, ps_tec_t tec, ps_params_t *p, ps_err_t *err) {
    memset(p, 0, sizeof *p);
    p->tec = tec;
    switch (tec) {
    case PS_TEC_CV:
        U8(o, "cycles", p->p.cv.cycles); I16(o, "start", p->p.cv.start); I16(o, "end", p->p.cv.end);
        I16(o, "vertex1", p->p.cv.vertex1); I16(o, "vertex2", p->p.cv.vertex2); I16(o, "step", p->p.cv.step);
        U16(o, "rate_mV_s", p->p.cv.rate_mv_s);
        break;
    case PS_TEC_SWV:
        I16(o, "start", p->p.swv.start); I16(o, "end", p->p.swv.end); I16(o, "amp", p->p.swv.amp);
        I16(o, "step", p->p.swv.step);
        if (!get_f(o, "freq_hz", &p->p.swv.freq_hz, err)) return false;
        break;
    case PS_TEC_DPV:
        I16(o, "start", p->p.dpv.start); I16(o, "end", p->p.dpv.end); I16(o, "amp", p->p.dpv.amp);
        I16(o, "step", p->p.dpv.step); U32(o, "period_ms", p->p.dpv.period_ms); U32(o, "width_ms", p->p.dpv.width_ms);
        break;
    case PS_TEC_NPV:
        I16(o, "start", p->p.npv.start); I16(o, "end", p->p.npv.end); I16(o, "amp", p->p.npv.amp);
        U32(o, "width_ms", p->p.npv.width_ms); U32(o, "period_ms", p->p.npv.period_ms); U32(o, "quiet_ms", p->p.npv.quiet_ms);
        break;
    case PS_TEC_CA:
        I16(o, "pre_step", p->p.ca.pre_step); U32(o, "quiet_ms", p->p.ca.quiet_ms); I16(o, "v1", p->p.ca.v1);
        U32(o, "t1_ms", p->p.ca.t1_ms); I16(o, "v2", p->p.ca.v2); U32(o, "t2_ms", p->p.ca.t2_ms);
        U16(o, "samples", p->p.ca.samples);
        break;
    }
    return true;
}

static bool get_win(const cJSON *o, const char *key, ps_win_t *w, ps_err_t *err) {
    const cJSON *a = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!a) return bad(err, PS_ERR_MISSING, key, 0, "falta el campo");
    if (!cJSON_IsArray(a)) return bad(err, PS_ERR_TYPE, key, 0, "debe ser [lo, hi]");
    if (cJSON_GetArraySize(a) != 2) return bad(err, PS_ERR_RANGE, key, 2, "debe tener 2 elementos");
    double v[2];
    for (int i = 0; i < 2; i++) {
        const cJSON *it = cJSON_GetArrayItem(a, i);
        if (!cJSON_IsNumber(it)) return bad(err, PS_ERR_TYPE, key, 0, "debe ser un numero");
        v[i] = it->valuedouble;
        if (v[i] != floor(v[i])) return bad(err, PS_ERR_TYPE, key, 0, "debe ser entero");
        if (v[i] < INT16_MIN || v[i] > INT16_MAX) return bad(err, PS_ERR_RANGE, key, v[i] < INT16_MIN ? INT16_MIN : INT16_MAX, "fuera del rango del tipo");
    }
    w->lo = (int16_t)v[0]; w->hi = (int16_t)v[1];
    return true;
}

static bool parse_profile(const cJSON *o, ps_profile_t *p, ps_err_t *err) {
    memset(p, 0, sizeof *p);
    if (!get_s(o, "name", sizeof p->name - 1, p->name, err)) return false;
    I16(o, "e_min_mv", p->e_min_mv); I16(o, "e_max_mv", p->e_max_mv);
    U16(o, "step_min_mv", p->step_min_mv); U16(o, "step_max_mv", p->step_max_mv); U16(o, "amp_max_mv", p->amp_max_mv);
    if (!get_f(o, "freq_min_hz", &p->freq_min_hz, err) || !get_f(o, "freq_max_hz", &p->freq_max_hz, err)) return false;
    U16(o, "rate_min_mv_s", p->rate_min_mv_s); U16(o, "rate_max_mv_s", p->rate_max_mv_s);
    U8(o, "gain_mask", p->gain_mask); U8(o, "default_gain_idx", p->default_gain_idx); U8(o, "rload_idx", p->rload_idx);
    U32(o, "r_ext_ohm", p->r_ext_ohm); U32(o, "settle_us", p->settle_us); U16(o, "max_points", p->max_points);
    GI(o, "redox", p->redox, INT8_MIN, INT8_MAX);
    if (!get_win(o, "peak_win", &p->peak_win, err) || !get_win(o, "base_win1", &p->base_win1, err) ||
        !get_win(o, "base_win2", &p->base_win2, err)) return false;
    U8(o, "sat_pct", p->sat_pct); U8(o, "small_pct", p->small_pct); U8(o, "edge_pct", p->edge_pct);
    U8(o, "target_pct", p->target_pct); U8(o, "fwhm_k", p->fwhm_k);
    const cJSON *d, *dp;
    if (!get_obj(o, "defaults", &d, err)) return false;
    ps_tec_t tec;
    if (!get_tec(d, &tec, err) || !get_obj(d, "params", &dp, err)) return false;
    return parse_params(dp, tec, &p->defaults, err);
}

static bool parse_cal(const cJSON *o, ps_cal_t *c, ps_err_t *err) {
    static const char *const key[2] = {"offset_na", "gain"};
    float *dst[2] = {c->offset_na, c->gain};
    for (int k = 0; k < 2; k++) {
        const cJSON *a = cJSON_GetObjectItemCaseSensitive(o, key[k]);
        if (!a) return bad(err, PS_ERR_MISSING, key[k], 0, "falta el campo");
        if (!cJSON_IsArray(a)) return bad(err, PS_ERR_TYPE, key[k], 0, "debe ser un arreglo");
        if (cJSON_GetArraySize(a) != 8) return bad(err, PS_ERR_RANGE, key[k], 8, "debe tener 8 elementos");
        for (int i = 0; i < 8; i++)
            if (!num_f(cJSON_GetArrayItem(a, i), key[k], &dst[k][i], err)) return false;
    }
    return true;
}

static bool parse_run(const cJSON *root, ps_cmd_t *out, ps_err_t *err) {
    if (!get_s(root, "id", ID_MAX, out->id, err)) return false;
    ps_tec_t tec;
    const cJSON *pr;
    if (!get_tec(root, &tec, err) || !get_obj(root, "params", &pr, err)) return false;
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(root, "auto");
    if (it && !cJSON_IsBool(it)) return bad(err, PS_ERR_TYPE, "auto", 0, "debe ser booleano");
    out->auto_adj = cJSON_IsTrue(it);
    out->gain_auto = true;
    it = cJSON_GetObjectItemCaseSensitive(root, "gain");
    if (it && cJSON_IsString(it)) {
        if (strcmp(it->valuestring, "auto")) return bad(err, PS_ERR_RANGE, "gain", 7, "debe ser \"auto\" o 0..7");
    } else if (it) {
        long long g;
        if (!get_i(root, "gain", 0, 7, &g, err)) return false;
        out->gain_auto = false; out->gain_idx = (uint8_t)g;
    }
    return parse_params(pr, tec, &out->params, err);
}

bool proto_parse_cmd(const char *json, size_t len, ps_cmd_t *out, ps_err_t *err) {
    static const char *const CMD[] = {"run", "stop", "set_profile", "get_profile", "select_profile", "set_calibration", "get_calibration"};
    memset(out, 0, sizeof *out);
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(json, len, &end, 0);
    if (root) {   /* no se admite basura después del objeto */
        while (end < json + len && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) end++;
        if (end != json + len) { cJSON_Delete(root); root = NULL; }
    }
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return bad(err, PS_ERR_PARSE, "", 0, "JSON mal formado");
    }
    bool ok = false;
    const cJSON *c = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    if (!c) { bad(err, PS_ERR_MISSING, "cmd", 0, "falta el campo"); goto done; }
    if (!cJSON_IsString(c)) { bad(err, PS_ERR_TYPE, "cmd", 0, "debe ser texto"); goto done; }
    int k;
    for (k = 0; k < 7; k++) if (!strcmp(c->valuestring, CMD[k])) break;
    if (k == 7) { bad(err, PS_ERR_RANGE, "cmd", 0, "comando desconocido"); goto done; }
    out->kind = (ps_cmd_kind_t)k;
    const cJSON *sub;
    switch (out->kind) {
    case PS_CMD_RUN: ok = parse_run(root, out, err); break;
    case PS_CMD_SET_PROFILE: ok = get_obj(root, "profile", &sub, err) && parse_profile(sub, &out->profile, err); break;
    case PS_CMD_SELECT_PROFILE: ok = get_s(root, "name", sizeof out->name - 1, out->name, err); break;
    case PS_CMD_SET_CAL: ok = get_obj(root, "cal", &sub, err) && parse_cal(sub, &out->cal, err); break;
    default: ok = true; break;   /* stop, get_profile, get_calibration */
    }
done:
    cJSON_Delete(root);
    if (!ok) memset(out, 0, sizeof *out);   /* un comando inválido no deja datos a medio llenar */
    return ok;
}

/* ------------------------------------------------------------ escritura */

typedef struct { char *b; size_t cap, len; bool bad; } W;

static void w_init(W *w, char *buf, size_t cap) { w->b = buf; w->cap = cap; w->len = 0; w->bad = (cap == 0); }
static void w_printf(W *w, const char *fmt, ...) {
    if (w->bad) return;
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(w->b + w->len, w->cap - w->len, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= w->cap - w->len) w->bad = true; else w->len += (size_t)n;
}
/* Los no finitos no son JSON válido: van como null. */
static void w_f(W *w, const char *fmt, float v) {
    if (isfinite(v)) w_printf(w, fmt, (double)v); else w_printf(w, "null");
}
static void w_str(W *w, const char *s) {
    w_printf(w, "\"");
    for (s = s ? s : ""; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') w_printf(w, "\\%c", c);
        else if (c < 0x20) w_printf(w, "\\u%04x", c);
        else w_printf(w, "%c", c);
    }
    w_printf(w, "\"");
}
static int w_done(const W *w) { return w->bad ? -1 : (int)w->len; }   /* NUL siempre dentro de cap */

#define FF "%.9g"   /* float exacto en ida y vuelta */

static void w_params(W *w, const ps_params_t *p) {
    w_printf(w, "{");
    switch (p->tec) {
    case PS_TEC_CV:
        w_printf(w, "\"cycles\":%u,\"start\":%d,\"end\":%d,\"vertex1\":%d,\"vertex2\":%d,\"step\":%d,\"rate_mV_s\":%u",
                 p->p.cv.cycles, p->p.cv.start, p->p.cv.end, p->p.cv.vertex1, p->p.cv.vertex2, p->p.cv.step, p->p.cv.rate_mv_s);
        break;
    case PS_TEC_SWV:
        w_printf(w, "\"start\":%d,\"end\":%d,\"amp\":%d,\"step\":%d,\"freq_hz\":", p->p.swv.start, p->p.swv.end, p->p.swv.amp, p->p.swv.step);
        w_f(w, FF, p->p.swv.freq_hz);
        break;
    case PS_TEC_DPV:
        w_printf(w, "\"start\":%d,\"end\":%d,\"amp\":%d,\"step\":%d,\"period_ms\":%" PRIu32 ",\"width_ms\":%" PRIu32,
                 p->p.dpv.start, p->p.dpv.end, p->p.dpv.amp, p->p.dpv.step, p->p.dpv.period_ms, p->p.dpv.width_ms);
        break;
    case PS_TEC_NPV:
        w_printf(w, "\"start\":%d,\"end\":%d,\"amp\":%d,\"width_ms\":%" PRIu32 ",\"period_ms\":%" PRIu32 ",\"quiet_ms\":%" PRIu32,
                 p->p.npv.start, p->p.npv.end, p->p.npv.amp, p->p.npv.width_ms, p->p.npv.period_ms, p->p.npv.quiet_ms);
        break;
    case PS_TEC_CA:
        w_printf(w, "\"pre_step\":%d,\"quiet_ms\":%" PRIu32 ",\"v1\":%d,\"t1_ms\":%" PRIu32 ",\"v2\":%d,\"t2_ms\":%" PRIu32 ",\"samples\":%u",
                 p->p.ca.pre_step, p->p.ca.quiet_ms, p->p.ca.v1, p->p.ca.t1_ms, p->p.ca.v2, p->p.ca.t2_ms, p->p.ca.samples);
        break;
    }
    w_printf(w, "}");
}
static const char *tec_name(ps_tec_t t) { return (unsigned)t < NUM_TEC ? TEC_NAME[t] : "?"; }
static const char *pass_name(ps_pass_t p) { return p == PS_PASS_EXPLORE ? "explore" : "final"; }

int proto_write_status(char *buf, size_t cap, const char *state, const ps_err_t *err) {
    static const char *const CODE[] = {NULL, "parse", "missing", "type", "range", "busy", "hw"};
    W w; w_init(&w, buf, cap);
    w_printf(&w, "{\"state\":"); w_str(&w, state);
    if (err && err->code != PS_ERR_NONE && (unsigned)err->code < 7) {
        w_printf(&w, ",\"code\":\"%s\",\"field\":", CODE[err->code]); w_str(&w, err->field);
        w_printf(&w, ",\"limit\":%" PRId32 ",\"msg\":", err->limit); w_str(&w, err->msg);
    }
    w_printf(&w, "}");
    return w_done(&w);
}

int proto_write_header(char *buf, size_t cap, const ps_run_meta_t *m) {
    W w; w_init(&w, buf, cap);
    w_printf(&w, "{\"id\":"); w_str(&w, m->id);
    w_printf(&w, ",\"pass\":\"%s\",\"tec\":\"%s\",\"params\":", pass_name(m->pass), tec_name(m->params->tec));
    w_params(&w, m->params);
    w_printf(&w, ",\"profile\":"); w_str(&w, m->profile_name);
    w_printf(&w, ",\"gain_idx\":%u,\"rload_idx\":%u,\"pga_idx\":%u,\"dac_vref_mv\":%" PRIu32 ",\"n_samples\":%" PRIu32
                 ",\"t0_us\":%" PRIu64 ",\"fw_version\":",
             m->gain_idx, m->rload_idx, m->pga_idx, m->dac_vref_mv, m->n_samples, m->t0_us);
    w_str(&w, m->fw_version);
    w_printf(&w, ",\"r_ext_ohm\":%" PRIu32 ",\"cal_offset_nA\":", m->r_ext_ohm); w_f(&w, FF, m->cal_offset_na);
    w_printf(&w, ",\"cal_gain\":"); w_f(&w, FF, m->cal_gain);
    w_printf(&w, ",\"cols\":[\"t_us\",\"tag\",\"cycle\",\"branch\",\"x_mV\",\"E_set_mV\",\"E_apl_uV\",\"sign\",\"bias_idx\","
                 "\"dac_code\",\"adc_raw\",\"pga\",\"dr\",\"i_nA\",\"flags\"]}");
    return w_done(&w);
}

#define TAIL_RESERVE 20   /* "],\"last\":false}" + NUL */

int proto_write_rows(char *buf, size_t cap, const char *id, ps_pass_t pass, uint32_t seq,
                     const ps_sample_t *s, const float *i_na, size_t n, size_t *consumed, bool *last) {
    W w; w_init(&w, buf, cap);
    w_printf(&w, "{\"id\":"); w_str(&w, id);
    w_printf(&w, ",\"pass\":\"%s\",\"seq\":%" PRIu32 ",\"rows\":[", pass_name(pass), seq);
    if (w.bad || w.len + TAIL_RESERVE > cap) return -1;
    size_t k;
    for (k = 0; k < n; k++) {
        size_t snap = w.len;
        const ps_sample_t *r = &s[k];
        w_printf(&w, "%s[%" PRIu32 ",%u,%u,%u,%d,%d,%" PRId32 ",%d,%u,%u,%d,%u,%u,", k ? "," : "", r->t_us, r->tag, r->cycle,
                 r->branch, r->x_mv, r->e_set_mv, r->e_apl_uv, r->sign, r->bias_idx, r->dac_code, r->adc_raw, r->pga, r->dr);
        w_f(&w, "%.3f", i_na[k]);
        w_printf(&w, ",%u]", r->flags);
        if (w.bad || w.len + TAIL_RESERVE > cap) { w.len = snap; w.bad = false; break; }   /* esta fila ya no cabe */
    }
    if (k == 0 && n > 0) return -1;
    *consumed = k;
    *last = (k == n);
    w_printf(&w, "],\"last\":%s}", *last ? "true" : "false");
    return w_done(&w);
}

int proto_write_result(char *buf, size_t cap, const char *id, ps_tec_t tec, const ps_features_t *f,
                       const ps_adj_t *adj, size_t n_adj, uint32_t run_flags) {
    static const char *const RULE[] = {"saturation", "small_signal", "edge", "zoom", "step_budget"};
    static const struct { uint32_t bit; const char *name; } FLAG[] = {
        {PS_RUN_NO_PEAK, "no_peak"}, {PS_RUN_PEAK_AT_EDGE, "peak_at_edge"}, {PS_RUN_ABORTED, "aborted"}, {PS_RUN_ERROR, "error"}};
    W w; w_init(&w, buf, cap);
    w_printf(&w, "{\"id\":"); w_str(&w, id);
    w_printf(&w, ",\"features\":");
    if (tec == PS_TEC_CA) {
        w_printf(&w, "{\"iss_nA\":[");
        for (int i = 0; i < 3; i++) { if (i) w_printf(&w, ","); w_f(&w, FF, f->ca.iss_na[i]); }
        w_printf(&w, "],\"q_nC\":[");
        for (int i = 0; i < 3; i++) { if (i) w_printf(&w, ","); w_f(&w, FF, f->ca.q_nc[i]); }
        w_printf(&w, "]}");
    } else if (f->has_peak) {
        w_printf(&w, "{\"baseline_m\":"); w_f(&w, FF, f->pk.base_m);
        w_printf(&w, ",\"baseline_b\":"); w_f(&w, FF, f->pk.base_b);
        w_printf(&w, ",\"ip_nA\":"); w_f(&w, FF, f->pk.ip_na);
        w_printf(&w, ",\"Ep_mV\":%d,\"area_nA_mV\":", f->pk.ep_mv); w_f(&w, FF, f->pk.area_na_mv);
        w_printf(&w, ",\"fwhm_mV\":"); w_f(&w, FF, f->pk.fwhm_mv);
        w_printf(&w, "}");
    } else {
        w_printf(&w, "null");
    }
    w_printf(&w, ",\"adjustments\":[");
    for (size_t i = 0; i < n_adj; i++)
        w_printf(&w, "%s{\"rule\":\"%s\",\"before\":[%" PRId32 ",%" PRId32 "],\"after\":[%" PRId32 ",%" PRId32 "]}", i ? "," : "",
                 (unsigned)adj[i].rule < 5 ? RULE[adj[i].rule] : "?", adj[i].b0, adj[i].b1, adj[i].a0, adj[i].a1);
    w_printf(&w, "],\"flags\":[");
    bool first = true;
    for (size_t i = 0; i < sizeof FLAG / sizeof FLAG[0]; i++)
        if (run_flags & FLAG[i].bit) { w_printf(&w, "%s\"%s\"", first ? "" : ",", FLAG[i].name); first = false; }
    w_printf(&w, "]}");
    return w_done(&w);
}

static void w_win(W *w, const char *key, ps_win_t v) { w_printf(w, ",\"%s\":[%d,%d]", key, v.lo, v.hi); }

int proto_write_profile(char *buf, size_t cap, const ps_profile_t *p) {
    W w; w_init(&w, buf, cap);
    w_printf(&w, "{\"name\":"); w_str(&w, p->name);
    w_printf(&w, ",\"e_min_mv\":%d,\"e_max_mv\":%d,\"step_min_mv\":%u,\"step_max_mv\":%u,\"amp_max_mv\":%u,\"freq_min_hz\":",
             p->e_min_mv, p->e_max_mv, p->step_min_mv, p->step_max_mv, p->amp_max_mv);
    w_f(&w, FF, p->freq_min_hz);
    w_printf(&w, ",\"freq_max_hz\":"); w_f(&w, FF, p->freq_max_hz);
    w_printf(&w, ",\"rate_min_mv_s\":%u,\"rate_max_mv_s\":%u,\"gain_mask\":%u,\"default_gain_idx\":%u,\"rload_idx\":%u,"
                 "\"r_ext_ohm\":%" PRIu32 ",\"settle_us\":%" PRIu32 ",\"max_points\":%u,\"redox\":%d",
             p->rate_min_mv_s, p->rate_max_mv_s, p->gain_mask, p->default_gain_idx, p->rload_idx, p->r_ext_ohm, p->settle_us,
             p->max_points, p->redox);
    w_win(&w, "peak_win", p->peak_win); w_win(&w, "base_win1", p->base_win1); w_win(&w, "base_win2", p->base_win2);
    w_printf(&w, ",\"sat_pct\":%u,\"small_pct\":%u,\"edge_pct\":%u,\"target_pct\":%u,\"fwhm_k\":%u,\"defaults\":{\"tec\":\"%s\",\"params\":",
             p->sat_pct, p->small_pct, p->edge_pct, p->target_pct, p->fwhm_k, tec_name(p->defaults.tec));
    w_params(&w, &p->defaults);
    w_printf(&w, "}}");
    return w_done(&w);
}

int proto_write_cal(char *buf, size_t cap, const ps_cal_t *c) {
    W w; w_init(&w, buf, cap);
    w_printf(&w, "{\"offset_na\":[");
    for (int i = 0; i < 8; i++) { if (i) w_printf(&w, ","); w_f(&w, FF, c->offset_na[i]); }
    w_printf(&w, "],\"gain\":[");
    for (int i = 0; i < 8; i++) { if (i) w_printf(&w, ","); w_f(&w, FF, c->gain[i]); }
    w_printf(&w, "]}");
    return w_done(&w);
}
