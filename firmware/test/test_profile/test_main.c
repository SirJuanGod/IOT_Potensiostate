#include <string.h>
#include <unity.h>
#include "profile.h"

void setUp(void) {}
void tearDown(void) {}

static ps_params_t swv(int s, int e, int amp, int step, float f) {
    ps_params_t p = {.tec = PS_TEC_SWV}; p.p.swv = (ps_swv_t){s, e, amp, step, f}; return p;
}
static void expect_field(const ps_err_t *e, const char *field, int32_t limit) {
    TEST_ASSERT_EQUAL_STRING(field, e->field);
    TEST_ASSERT_EQUAL_INT32(limit, e->limit);
    TEST_ASSERT_EQUAL(PS_ERR_RANGE, e->code);
}

void test_default_is_valid(void) {
    ps_profile_t p; ps_err_t e; profile_default(&p);
    TEST_ASSERT_TRUE(profile_check(&p, 3300, &e));
    TEST_ASSERT_EQUAL_STRING("default", p.name);
    TEST_ASSERT_EQUAL(PS_TEC_SWV, p.defaults.tec);
    TEST_ASSERT_TRUE(profile_validate_run(&p, &p.defaults, 3, 3300, &e));
}
void test_validate_accepts_zero_crossing(void) {
    ps_profile_t p; ps_err_t e; profile_default(&p);
    ps_params_t r = swv(-400, 500, 50, 1, 25.0f);
    TEST_ASSERT_TRUE(profile_validate_run(&p, &r, 3, 3300, &e));
}
void test_validate_rejects_amp(void) {
    ps_profile_t p; ps_err_t e; profile_default(&p);
    ps_params_t r = swv(-400, 500, 150, 1, 25.0f);
    TEST_ASSERT_FALSE(profile_validate_run(&p, &r, 3, 3300, &e));
    expect_field(&e, "amp", 100);
}
void test_validate_rejects_too_fast(void) {
    ps_profile_t p; ps_err_t e; profile_default(&p); p.freq_max_hz = 1000;
    ps_params_t r = swv(-400, 500, 50, 1, 250.0f);   /* medio paso 2000 us < 2280 us */
    TEST_ASSERT_FALSE(profile_validate_run(&p, &r, 3, 3300, &e));
    TEST_ASSERT_EQUAL_STRING("freq_hz", e.field);
}
void test_validate_rejects_points(void) {
    ps_profile_t p; ps_err_t e; profile_default(&p); p.max_points = 1000;
    ps_params_t r = swv(-400, 500, 50, 1, 25.0f);    /* 1802 muestras */
    TEST_ASSERT_FALSE(profile_validate_run(&p, &r, 3, 3300, &e));
    expect_field(&e, "points", 1000);
}
void test_validate_rejects_ext_gain_without_r(void) {
    ps_profile_t p; ps_err_t e; profile_default(&p); p.gain_mask = 0xFF;
    ps_params_t r = swv(-400, 500, 50, 1, 25.0f);
    TEST_ASSERT_FALSE(profile_validate_run(&p, &r, PS_GAIN_EXT, 3300, &e));
    TEST_ASSERT_EQUAL_STRING("gain", e.field);
    p.r_ext_ohm = 10000;
    TEST_ASSERT_TRUE(profile_validate_run(&p, &r, PS_GAIN_EXT, 3300, &e));
}
void test_validate_rejects_out_of_profile(void) {
    ps_profile_t p; ps_err_t e; profile_default(&p);
    ps_params_t r = {.tec = PS_TEC_CV}; r.p.cv = (ps_cv_t){1, 0, 0, 650, -300, 5, 100};
    TEST_ASSERT_FALSE(profile_validate_run(&p, &r, 3, 3300, &e));
    expect_field(&e, "vertex1", 600);
}
void test_validate_rejects_cv_offgrid(void) {
    ps_profile_t p; ps_err_t e; profile_default(&p);
    ps_params_t r = {.tec = PS_TEC_CV}; r.p.cv = (ps_cv_t){1, 0, 0, 12, -300, 5, 100};
    TEST_ASSERT_FALSE(profile_validate_run(&p, &r, 3, 3300, &e));
    expect_field(&e, "step", 5);
    r.p.cv.vertex1 = 10;                              /* 10 y 310 son múltiplos de 5, 300 también */
    TEST_ASSERT_TRUE(profile_validate_run(&p, &r, 3, 3300, &e));
}
void test_validate_rejects_width_gt_period(void) {
    ps_profile_t p; ps_err_t e; profile_default(&p);
    ps_params_t r = {.tec = PS_TEC_DPV}; r.p.dpv = (ps_dpv_t){0, 100, 50, 10, 50, 100};
    TEST_ASSERT_FALSE(profile_validate_run(&p, &r, 3, 3300, &e));
    expect_field(&e, "width_ms", 50);
}
void test_validate_rejects_t1_too_long(void) {
    ps_profile_t p; ps_err_t e; profile_default(&p);
    ps_params_t r = {.tec = PS_TEC_CA}; r.p.ca = (ps_ca_t){0, 100, -100, 100, 4000000, 100, 10};
    TEST_ASSERT_FALSE(profile_validate_run(&p, &r, 3, 3300, &e));
    expect_field(&e, "t1_ms", 3600000);
}
void test_profile_check_rejects_beyond_physical(void) {
    ps_profile_t p; ps_err_t e; profile_default(&p); p.e_max_mv = 750;
    TEST_ASSERT_FALSE(profile_check(&p, 3000, &e));
    TEST_ASSERT_EQUAL_STRING("e_max_mv", e.field);
}
void test_profile_check_rejects_window_outside(void) {
    ps_profile_t p; ps_err_t e; profile_default(&p); p.base_win2 = (ps_win_t){500, 700};
    TEST_ASSERT_FALSE(profile_check(&p, 3300, &e));
    TEST_ASSERT_EQUAL_STRING("base_win2", e.field);
}
void test_profile_check_rejects_zero_min(void) {
    ps_profile_t p; ps_err_t e; profile_default(&p); p.freq_min_hz = 0;
    TEST_ASSERT_FALSE(profile_check(&p, 3300, &e));
    TEST_ASSERT_EQUAL_STRING("freq_min_hz", e.field);
    profile_default(&p); p.rate_min_mv_s = 0;
    TEST_ASSERT_FALSE(profile_check(&p, 3300, &e));
    TEST_ASSERT_EQUAL_STRING("rate_min_mv_s", e.field);
}
void test_validate_rejects_unknown_tec(void) {
    ps_profile_t p; ps_err_t e; profile_default(&p);
    ps_params_t r = swv(-400, 500, 50, 1, 25.0f); r.tec = (ps_tec_t)9;
    TEST_ASSERT_FALSE(profile_validate_run(&p, &r, 3, 3300, &e));
    expect_field(&e, "tec", 0);
}
void test_profile_check_rejects_ext_default_without_r(void) {
    ps_profile_t p; ps_err_t e; profile_default(&p); p.gain_mask = 0xFF; p.default_gain_idx = PS_GAIN_EXT;
    TEST_ASSERT_FALSE(profile_check(&p, 3300, &e));
    TEST_ASSERT_EQUAL_STRING("default_gain_idx", e.field);
    p.r_ext_ohm = 10000;
    TEST_ASSERT_TRUE(profile_check(&p, 3300, &e));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_default_is_valid); RUN_TEST(test_validate_accepts_zero_crossing);
    RUN_TEST(test_validate_rejects_amp); RUN_TEST(test_validate_rejects_too_fast);
    RUN_TEST(test_validate_rejects_points); RUN_TEST(test_validate_rejects_ext_gain_without_r);
    RUN_TEST(test_validate_rejects_out_of_profile); RUN_TEST(test_validate_rejects_cv_offgrid);
    RUN_TEST(test_validate_rejects_width_gt_period); RUN_TEST(test_validate_rejects_t1_too_long);
    RUN_TEST(test_profile_check_rejects_beyond_physical); RUN_TEST(test_profile_check_rejects_window_outside);
    RUN_TEST(test_profile_check_rejects_zero_min); RUN_TEST(test_validate_rejects_unknown_tec);
    RUN_TEST(test_profile_check_rejects_ext_default_without_r);
    return UNITY_END();
}
