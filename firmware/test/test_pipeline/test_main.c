/* Extremo a extremo: technique → muestras → analysis_features, con una onda reversible sintética. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unity.h>
#include "technique.h"
#include "analysis.h"

void setUp(void) {}
void tearDown(void) {}

#define E0 100          /* mV */
#define I_LIM 1000.0f   /* nA */
#define NMAX 2000

/* onda reversible: oxidación (+1) sube a +I sobre E0; reducción (−1) baja a −I bajo E0 */
static float wave(float e, int8_t redox) { return redox * I_LIM / (1.0f + expf(-redox * (e - E0) / 25.0f)); }

static ps_sample_t S[NMAX];
static float I[NMAX], YB[NMAX];
static int16_t XB[NMAX];

static void check(const ps_params_t *p, int8_t redox, int tol, const char *what) {
    ps_profile_t pr; ps_err_t e; ps_features_t f; char msg[96];
    profile_default(&pr);
    pr.redox = redox;
    pr.peak_win = (ps_win_t){-200, 400};
    pr.base_win1 = (ps_win_t){-295, -200}; pr.base_win2 = (ps_win_t){400, 495};
    snprintf(msg, sizeof msg, "%s redox %d", what, redox);
    TEST_ASSERT_TRUE_MESSAGE(profile_validate_run(&pr, p, 3, 3300, &e), e.field);

    size_t n = 0;
    float q_prev = 0;
    ps_step_t st;
    for (uint32_t k = 0; tech_step_at(p, k, &st); k++) {
        float q = wave(st.e_mv, redox);
        if (st.sample) {
            S[n] = (ps_sample_t){.x_mv = st.x_mv, .e_set_mv = st.e_mv, .tag = st.tag, .cycle = st.cycle, .branch = st.branch};
            /* CV: corriente = dQ/dt de la misma onda (par adsorbido reversible); el resto, i(E) muestreada */
            I[n] = p->tec == PS_TEC_CV ? (k ? q - q_prev : 0.0f) : q;
            n++;
        }
        q_prev = q;
    }
    TEST_ASSERT_TRUE_MESSAGE(analysis_features(S, I, n, p->tec, &pr, XB, YB, &f), msg);
    TEST_ASSERT_TRUE_MESSAGE(f.has_peak, msg);
    snprintf(msg, sizeof msg, "%s redox %d: Ep %d fwhm %.1f", what, redox, f.pk.ep_mv, (double)f.pk.fwhm_mv);
    TEST_ASSERT_TRUE_MESSAGE(f.pk.ip_na * redox > 0, msg);
    if (tol >= 0) TEST_ASSERT_INT_WITHIN_MESSAGE(tol, E0, f.pk.ep_mv, msg);
    /* dQ/dt de la sigmoide: FWHM = 2·25·ln(3 + 2√2) = 88,1 mV (detecta una rama de CV partida) */
    if (p->tec == PS_TEC_CV) TEST_ASSERT_FLOAT_WITHIN_MESSAGE(5.0f, 88.1f, f.pk.fwhm_mv, msg);
}

/* barrido hacia arriba para oxidación y hacia abajo para reducción: los dos sentidos y los dos signos */
#define UP_DOWN(mk, ...) do { ps_params_t p = mk(-300, 500, __VA_ARGS__); check(&p, 1, TOL, #mk " sube"); \
                              p = mk(500, -300, __VA_ARGS__); check(&p, -1, TOL, #mk " baja"); } while (0)

static ps_params_t swv(int s, int e, int amp, int step) {
    ps_params_t p = {.tec = PS_TEC_SWV}; p.p.swv = (ps_swv_t){s, e, amp, step, 25.0f}; return p;
}
static ps_params_t dpv(int s, int e, int amp, int step) {
    ps_params_t p = {.tec = PS_TEC_DPV}; p.p.dpv = (ps_dpv_t){s, e, amp, step, 100, 50}; return p;
}
static ps_params_t npv(int s, int e, int amp) {
    ps_params_t p = {.tec = PS_TEC_NPV}; p.p.npv = (ps_npv_t){s, e, amp, 50, 200, 500}; return p;
}
static ps_params_t cv(int cycles, int s, int v1, int v2, int e) {
    ps_params_t p = {.tec = PS_TEC_CV}; p.p.cv = (ps_cv_t){(uint8_t)cycles, s, e, v1, v2, 5, 100}; return p;
}

void test_swv(void) {
#define TOL (2 * 5 + 25)
    UP_DOWN(swv, 25, 5);
#undef TOL
}
void test_dpv(void) {
#define TOL (2 * 5 + 25)
    UP_DOWN(dpv, 25, 5);
#undef TOL
}
/* NPV da una onda sigmoidal (i(x) − i(start)), no un pico: el máximo sobre la línea base cae en la
 * meseta (Ep ≈ E0 + 80 mV con estas ventanas), así que aquí solo se exige pico y signo; ver informe. */
void test_npv(void) {
#define TOL (-1)
    UP_DOWN(npv, 10);
#undef TOL
}
void test_cv(void) {
    const int8_t rx[2] = {1, -1};
    for (int r = 0; r < 2; r++) {
        ps_params_t p = cv(1, -300, 500, -300, -300);   /* sube primero */
        check(&p, rx[r], 2 * 5, "cv sube primero");
        p = cv(1, 500, -300, 500, 500);                 /* baja primero */
        check(&p, rx[r], 2 * 5, "cv baja primero");
        p = cv(2, E0, 500, -300, E0);                   /* start entre vértices, justo en E0 */
        check(&p, rx[r], 2 * 5, "cv start en medio");
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_swv);
    RUN_TEST(test_dpv);
    RUN_TEST(test_npv);
    RUN_TEST(test_cv);
    return UNITY_END();
}
