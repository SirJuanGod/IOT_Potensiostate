/*
 Este archivo reimplementa ideas de MiniStatAnalyst (LinnesLab); no se copia su código.

 DISCLAIMER
 Linnes Lab code, firmware, and software is released under the MIT License
 (http://opensource.org/licenses/MIT).
 
 The MIT License (MIT)
 
 Copyright (c) 2019 Linnes Lab, Purdue University, West Lafayette, IN, USA
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights to
 use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 of the Software, and to permit persons to whom the Software is furnished to do
 so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.
 */
/* Ver analysis.h. */
#include <math.h>
#include "analysis.h"

/* Empareja muestras consecutivas (a_tag, b_tag) con el mismo x_mv; y = (a − b) o (b − a) según neg.
 * Un par incompleto se descarta saltando solo la primera muestra. */
static size_t build_pairs(const ps_sample_t *s, const float *i, size_t n, uint8_t a_tag, uint8_t b_tag,
                          bool neg, int16_t *x, float *y) {
    size_t cnt = 0;
    for (size_t k = 0; k + 1 < n;) {
        if (s[k].tag == a_tag && s[k + 1].tag == b_tag && s[k].x_mv == s[k + 1].x_mv) {
            x[cnt] = s[k].x_mv;
            y[cnt] = neg ? i[k + 1] - i[k] : i[k] - i[k + 1];
            cnt++; k += 2;
        } else k++;
    }
    return cnt;
}

size_t analysis_build_curve(const ps_sample_t *s, const float *i_na, size_t n, ps_tec_t tec, int8_t redox, int16_t *x_mv, float *y_na) {
    switch (tec) {
    case PS_TEC_SWV: return build_pairs(s, i_na, n, PS_TAG_FWD, PS_TAG_REV, false, x_mv, y_na);
    case PS_TEC_DPV: return build_pairs(s, i_na, n, PS_TAG_BASE, PS_TAG_PULSE, true, x_mv, y_na);
    case PS_TEC_NPV: return build_pairs(s, i_na, n, PS_TAG_PULSE, PS_TAG_BASE, false, x_mv, y_na);
    case PS_TEC_CV: {
        uint8_t last = 0, br = redox > 0 ? PS_BR_UP : PS_BR_DOWN;
        size_t cnt = 0;
        for (size_t k = 0; k < n; k++) if (s[k].tag == PS_TAG_STEP && s[k].cycle > last) last = s[k].cycle;
        for (size_t k = 0; k < n; k++)
            if (s[k].tag == PS_TAG_STEP && s[k].cycle == last && s[k].branch == br) { x_mv[cnt] = s[k].x_mv; y_na[cnt] = i_na[k]; cnt++; }
        return cnt;
    }
    default: return 0;
    }
}

static bool in_win(int16_t x, ps_win_t w) { return x >= w.lo && x <= w.hi; }

bool analysis_baseline(const int16_t *x, const float *y, size_t n, ps_win_t w1, ps_win_t w2, float *m, float *b) {
    size_t cnt = 0; float sx = 0, sy = 0;
    for (size_t k = 0; k < n; k++) if (in_win(x[k], w1) || in_win(x[k], w2)) { sx += x[k]; sy += y[k]; cnt++; }
    if (cnt < 2) return false;
    float mx = sx / (float)cnt, my = sy / (float)cnt, sxx = 0, sxy = 0;   /* centrado: evita perder precisión en float */
    for (size_t k = 0; k < n; k++)
        if (in_win(x[k], w1) || in_win(x[k], w2)) { float dx = x[k] - mx; sxx += dx * dx; sxy += dx * (y[k] - my); }
    if (sxx <= 0.0f) return false;   /* todos los puntos con el mismo x */
    *m = sxy / sxx; *b = my - *m * mx;
    return true;
}

/* Cruce lineal de nivel h entre (xa, ca) y (xb, cb). */
static float cross(float xa, float ca, float xb, float cb, float h) {
    return xa + (h - ca) * (xb - xa) / (cb - ca);
}

bool analysis_peak(const int16_t *x, const float *y, size_t n, float m, float b, ps_win_t search, int8_t redox, uint8_t edge_pct, ps_peak_feat_t *out) {
    size_t first = n, last = 0, cnt = 0, pk = n;
    float cmax = 0;
    for (size_t k = 0; k < n; k++) {
        if (!in_win(x[k], search)) continue;
        float c = redox * (y[k] - (m * x[k] + b));
        if (first == n) first = k;
        last = k; cnt++;
        if (pk == n || c > cmax) { cmax = c; pk = k; }
    }
    if (cnt < 3 || cmax <= 0.0f) return false;

    /* Región contigua >= media altura alrededor del pico, dentro de la ventana de búsqueda. */
    float h = cmax / 2;
    #define C(k) (redox * (y[k] - (m * x[k] + b)))
    size_t il = pk, ir = pk;
    while (il > first && C(il - 1) >= h) il--;
    while (ir < last && C(ir + 1) >= h) ir++;
    float xl = x[il], xr = x[ir], area = 0;   /* sin cruce: borde del rango */
    if (il > first) {
        xl = cross(x[il - 1], C(il - 1), x[il], C(il), h);
        area += 0.5f * (h + C(il)) * fabsf(x[il] - xl);
    }
    if (ir < last) {
        xr = cross(x[ir], C(ir), x[ir + 1], C(ir + 1), h);
        area += 0.5f * (h + C(ir)) * fabsf(xr - x[ir]);
    }
    for (size_t k = il; k < ir; k++) area += 0.5f * (C(k) + C(k + 1)) * fabsf((float)(x[k + 1] - x[k]));
    #undef C

    int16_t xmin = x[0], xmax = x[0];
    for (size_t k = 1; k < n; k++) { if (x[k] < xmin) xmin = x[k]; if (x[k] > xmax) xmax = x[k]; }
    float margen = (float)(xmax - xmin) * edge_pct / 100.0f;

    out->base_m = m; out->base_b = b;
    out->ip_na = cmax * redox;
    out->ep_mv = x[pk];
    out->fwhm_mv = fabsf(xr - xl);
    out->area_na_mv = area * redox;
    out->at_edge = (float)(x[pk] - xmin) <= margen || (float)(xmax - x[pk]) <= margen;
    return true;
}

void analysis_ca(const ps_sample_t *s, const float *i_na, size_t n, ps_ca_feat_t *out) {
    for (uint8_t seg = 0; seg < 3; seg++) {
        size_t cnt = 0, idx = 0, tail, prev = n;
        double q = 0, sum = 0;
        for (size_t k = 0; k < n; k++) if (s[k].branch == seg) cnt++;
        tail = cnt - (cnt / 5 > 0 ? cnt / 5 : (cnt > 0 ? 1 : 0));   /* último 20 %, mínimo 1 muestra */
        size_t used = cnt - tail;
        for (size_t k = 0; k < n; k++) {
            if (s[k].branch != seg) continue;
            if (idx >= tail) sum += i_na[k];
            if (prev != n) q += 0.5 * (i_na[prev] + i_na[k]) * (double)(s[k].t_us - s[prev].t_us);
            prev = k; idx++;
        }
        out->iss_na[seg] = used ? (float)(sum / (double)used) : 0.0f;
        out->q_nc[seg] = (float)(q / 1e6);   /* nA·µs = 1e-6 nC */
    }
}

bool analysis_features(const ps_sample_t *s, const float *i_na, size_t n, ps_tec_t tec, const ps_profile_t *prof, int16_t *x_buf, float *y_buf, ps_features_t *out) {
    float m, b;
    *out = (ps_features_t){0};
    if (tec == PS_TEC_CA) { analysis_ca(s, i_na, n, &out->ca); return true; }
    size_t np = analysis_build_curve(s, i_na, n, tec, prof->redox, x_buf, y_buf);
    if (np == 0) return false;
    out->has_peak = analysis_baseline(x_buf, y_buf, np, prof->base_win1, prof->base_win2, &m, &b)
                 && analysis_peak(x_buf, y_buf, np, m, b, prof->peak_win, prof->redox, prof->edge_pct, &out->pk);
    return true;
}
