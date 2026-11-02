#include <math.h>
#include <string.h>
#include <unity.h>
#include "analysis.h"

void setUp(void) {}
void tearDown(void) {}

#define NMAX 400
static int16_t X[NMAX];
static float Y[NMAX];

static size_t gauss(int lo, int hi, float sign) {
    size_t n = 0;
    for (int x = lo; x <= hi; x += 5, n++) {
        float d = (x - 100) / 40.0f;
        X[n] = (int16_t)x;
        Y[n] = 0.01f * x + 2.0f + sign * 100.0f * expf(-d * d);
    }
    return n;
}

void test_gaussian_peak(void) {
    size_t n = gauss(-500, 500, 1);
    float m, b; ps_peak_feat_t pk;
    TEST_ASSERT_TRUE(analysis_baseline(X, Y, n, (ps_win_t){-500, -300}, (ps_win_t){300, 500}, &m, &b));
    TEST_ASSERT_TRUE(analysis_peak(X, Y, n, m, b, (ps_win_t){-200, 300}, 1, 10, &pk));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.01f, m);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 100.0f, pk.ip_na);
    TEST_ASSERT_EQUAL_INT16(100, pk.ep_mv);
    TEST_ASSERT_FLOAT_WITHIN(5.0f, 66.6f, pk.fwhm_mv);
    TEST_ASSERT_FALSE(pk.at_edge);
    /* área analítica entre cruces: 100·40·sqrt(pi)·erf(sqrt(ln2)) ≈ 5.4e3 nA·mV */
    TEST_ASSERT_FLOAT_WITHIN(150.0f, 5385.0f, pk.area_na_mv);
}

void test_negative_peak(void) {
    size_t n = gauss(-500, 500, -1);
    float m, b; ps_peak_feat_t pk;
    TEST_ASSERT_TRUE(analysis_baseline(X, Y, n, (ps_win_t){-500, -300}, (ps_win_t){300, 500}, &m, &b));
    TEST_ASSERT_TRUE(analysis_peak(X, Y, n, m, b, (ps_win_t){-200, 300}, -1, 10, &pk));
    TEST_ASSERT_FLOAT_WITHIN(1.0f, -100.0f, pk.ip_na);
    TEST_ASSERT_EQUAL_INT16(100, pk.ep_mv);
    TEST_ASSERT_TRUE(pk.area_na_mv < 0);
}

void test_baseline_too_few_points(void) {
    size_t n = gauss(-100, 100, 1);
    float m, b;
    TEST_ASSERT_FALSE(analysis_baseline(X, Y, n, (ps_win_t){-500, -300}, (ps_win_t){300, 500}, &m, &b));
}

void test_peak_at_edge(void) {
    size_t n = gauss(-200, 110, 1);
    ps_peak_feat_t pk;
    TEST_ASSERT_TRUE(analysis_peak(X, Y, n, 0.01f, 2.0f, (ps_win_t){-200, 300}, 1, 10, &pk));
    TEST_ASSERT_TRUE(pk.at_edge);
}

void test_peak_too_few_points_or_not_positive(void) {
    size_t n = gauss(-500, 500, 1);
    ps_peak_feat_t pk;
    TEST_ASSERT_FALSE(analysis_peak(X, Y, n, 0.01f, 2.0f, (ps_win_t){100, 105}, 1, 10, &pk)); /* 2 puntos */
    TEST_ASSERT_FALSE(analysis_peak(X, Y, n, 0.01f, 2.0f, (ps_win_t){-200, 300}, -1, 10, &pk)); /* pico positivo, redox -1 */
}

void test_build_curve_swv(void) {
    ps_sample_t s[7]; float i[7]; memset(s, 0, sizeof s);
    for (int k = 0; k < 3; k++) {
        s[2*k].tag = PS_TAG_FWD; s[2*k+1].tag = PS_TAG_REV;
        s[2*k].x_mv = s[2*k+1].x_mv = (int16_t)(-100 + 10*k);
        i[2*k] = 10; i[2*k+1] = 4;
    }
    s[6].tag = PS_TAG_FWD; s[6].x_mv = -80; i[6] = 10;       /* par incompleto: se descarta */
    size_t n = analysis_build_curve(s, i, 7, PS_TEC_SWV, 1, X, Y);
    TEST_ASSERT_EQUAL_UINT(3, n);
    for (int k = 0; k < 3; k++) {
        TEST_ASSERT_EQUAL_INT16(-100 + 10*k, X[k]);
        TEST_ASSERT_EQUAL_FLOAT(6.0f, Y[k]);
    }
}

void test_build_curve_dpv_npv(void) {
    ps_sample_t s[4]; float i[4]; memset(s, 0, sizeof s);
    s[0].tag = PS_TAG_BASE;  s[1].tag = PS_TAG_PULSE; i[0] = 3; i[1] = 8;
    s[2].tag = PS_TAG_BASE;  s[3].tag = PS_TAG_PULSE; i[2] = 3; i[3] = 9;
    s[0].x_mv = s[1].x_mv = 10; s[2].x_mv = s[3].x_mv = 20;
    TEST_ASSERT_EQUAL_UINT(2, analysis_build_curve(s, i, 4, PS_TEC_DPV, 1, X, Y));
    TEST_ASSERT_EQUAL_FLOAT(5.0f, Y[0]); TEST_ASSERT_EQUAL_FLOAT(6.0f, Y[1]); TEST_ASSERT_EQUAL_INT16(20, X[1]);
    s[0].tag = PS_TAG_PULSE; s[1].tag = PS_TAG_BASE; i[0] = 8; i[1] = 3;
    s[2].tag = PS_TAG_PULSE; s[3].tag = PS_TAG_BASE; i[2] = 9; i[3] = 3;
    TEST_ASSERT_EQUAL_UINT(2, analysis_build_curve(s, i, 4, PS_TEC_NPV, 1, X, Y));
    TEST_ASSERT_EQUAL_FLOAT(5.0f, Y[0]); TEST_ASSERT_EQUAL_FLOAT(6.0f, Y[1]);
}

void test_build_curve_cv_last_cycle(void) {
    ps_sample_t s[12]; float i[12]; memset(s, 0, sizeof s);
    for (int k = 0; k < 12; k++) {
        s[k].tag = PS_TAG_STEP;
        s[k].cycle = (uint8_t)(k / 6);
        s[k].branch = (k % 6) < 3 ? PS_BR_UP : PS_BR_DOWN;
        s[k].x_mv = (int16_t)((k % 3) * 10);
        i[k] = (float)k;
    }
    size_t n = analysis_build_curve(s, i, 12, PS_TEC_CV, 1, X, Y);
    TEST_ASSERT_EQUAL_UINT(3, n);
    TEST_ASSERT_EQUAL_FLOAT(6.0f, Y[0]); TEST_ASSERT_EQUAL_FLOAT(8.0f, Y[2]);
    n = analysis_build_curve(s, i, 12, PS_TEC_CV, -1, X, Y);
    TEST_ASSERT_EQUAL_UINT(3, n);
    TEST_ASSERT_EQUAL_FLOAT(9.0f, Y[0]);
    TEST_ASSERT_EQUAL_UINT(0, analysis_build_curve(s, i, 12, PS_TEC_CA, 1, X, Y));
}

void test_ca_features(void) {
    ps_sample_t s[30]; float i[30]; memset(s, 0, sizeof s);
    for (int k = 0; k < 30; k++) {
        s[k].tag = PS_TAG_STEP; s[k].branch = (uint8_t)(k / 10);
        s[k].t_us = (uint32_t)k * 100000u; i[k] = 5.0f + 2.0f * (k / 10);
    }
    ps_ca_feat_t ca; analysis_ca(s, i, 30, &ca);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, ca.iss_na[0]);
    TEST_ASSERT_EQUAL_FLOAT(7.0f, ca.iss_na[1]);
    TEST_ASSERT_EQUAL_FLOAT(9.0f, ca.iss_na[2]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 4.5f, ca.q_nc[0]);
}

void test_features_orchestration(void) {
    ps_profile_t p; profile_default(&p);
    p.redox = 1; p.peak_win = (ps_win_t){-200, 300};
    p.base_win1 = (ps_win_t){-500, -300}; p.base_win2 = (ps_win_t){300, 500};
    ps_sample_t s[NMAX]; float i[NMAX]; memset(s, 0, sizeof s);
    size_t n = gauss(-500, 500, 1);
    for (size_t k = 0; k < n; k++) { s[k].tag = PS_TAG_STEP; s[k].x_mv = X[k]; i[k] = Y[k]; }
    int16_t xb[NMAX]; float yb[NMAX]; ps_features_t f;
    TEST_ASSERT_TRUE(analysis_features(s, i, n, PS_TEC_CV, &p, xb, yb, &f));
    TEST_ASSERT_TRUE(f.has_peak);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 100.0f, f.pk.ip_na);
    p.base_win1 = (ps_win_t){900, 950}; p.base_win2 = (ps_win_t){900, 950};
    TEST_ASSERT_TRUE(analysis_features(s, i, n, PS_TEC_CV, &p, xb, yb, &f));
    TEST_ASSERT_FALSE(f.has_peak);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_gaussian_peak);
    RUN_TEST(test_negative_peak);
    RUN_TEST(test_baseline_too_few_points);
    RUN_TEST(test_peak_at_edge);
    RUN_TEST(test_peak_too_few_points_or_not_positive);
    RUN_TEST(test_build_curve_swv);
    RUN_TEST(test_build_curve_dpv_npv);
    RUN_TEST(test_build_curve_cv_last_cycle);
    RUN_TEST(test_ca_features);
    RUN_TEST(test_features_orchestration);
    return UNITY_END();
}
