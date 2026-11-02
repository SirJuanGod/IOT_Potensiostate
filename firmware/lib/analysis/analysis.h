/*
 * Análisis de voltamogramas: línea base, pico y características para el modelo de IA.
 * Idea de MiniStatAnalyst (LinnesLab, licencia MIT) reimplementada; no se copia su código.
 * Sin dependencias de ESP-IDF: el mismo código corre en el host y en el equipo.
 */
#pragma once
#include <stddef.h>
#include "ps_common.h"
#include "profile.h"

typedef struct { float base_m, base_b, ip_na, area_na_mv, fwhm_mv; int16_t ep_mv; bool at_edge; } ps_peak_feat_t;
typedef struct { float iss_na[3], q_nc[3]; } ps_ca_feat_t;
typedef struct { bool has_peak; ps_peak_feat_t pk; ps_ca_feat_t ca; } ps_features_t;

/* Arma la curva (x_mv, y_na) según la técnica; x_mv/y_na deben tener capacidad n. Devuelve nº de puntos (CA: 0). */
size_t analysis_build_curve(const ps_sample_t *s, const float *i_na, size_t n, ps_tec_t tec, int8_t redox, int16_t *x_mv, float *y_na);
/* Recta y = m·x + b por mínimos cuadrados sobre los puntos en w1 ∪ w2; false si hay menos de 2. */
bool   analysis_baseline(const int16_t *x, const float *y, size_t n, ps_win_t w1, ps_win_t w2, float *m, float *b);
/* Pico dentro de `search`; false si hay menos de 3 puntos en la ventana o el máximo corregido es <= 0. */
bool   analysis_peak(const int16_t *x, const float *y, size_t n, float m, float b, ps_win_t search, int8_t redox, uint8_t edge_pct, ps_peak_feat_t *out);
/* Corriente estacionaria y carga por segmento (branch 0..2). */
void   analysis_ca(const ps_sample_t *s, const float *i_na, size_t n, ps_ca_feat_t *out);
/* x_buf/y_buf con capacidad n. Devuelve false si no hay curva (picos) o la técnica no produce datos. CA: siempre true. */
bool   analysis_features(const ps_sample_t *s, const float *i_na, size_t n, ps_tec_t tec, const ps_profile_t *prof, int16_t *x_buf, float *y_buf, ps_features_t *out);
