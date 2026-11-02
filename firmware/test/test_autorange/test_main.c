#include <string.h>
#include <unity.h>
#include "autorange.h"
#include "technique.h"

void setUp(void) {}
void tearDown(void) {}

static const ps_gain_knobs_t CUR = {3, 2};

static ps_params_t swv(int s, int e, int amp, int step) {
    ps_params_t p = {.tec = PS_TEC_SWV}; p.p.swv = (ps_swv_t){s, e, amp, step, 25.0f}; return p;
}
static ps_params_t dpv(int s, int e, int amp, int step) {
    ps_params_t p = {.tec = PS_TEC_DPV}; p.p.dpv = (ps_dpv_t){s, e, amp, step, 100, 50}; return p;
}
static ps_params_t npv(int s, int e, int amp) {
    ps_params_t p = {.tec = PS_TEC_NPV}; p.p.npv = (ps_npv_t){s, e, amp, 50, 200, 500}; return p;
}
/* fracción f del fondo de escala del ADC */
static uint16_t raw(float f) { return (uint16_t)(f * 32767); }
static autorange_input_t in_of(float max_frac, bool peak, float peak_frac) {
    autorange_input_t i = {raw(max_frac), peak, peak_frac, 100, 66.0f, false}; return i;
}
static size_t decide(const autorange_input_t *in, const ps_profile_t *pr, const ps_params_t *req,
                     ps_params_t *fp, ps_gain_knobs_t *fk, ps_adj_t *adj, bool *edge) {
    return autorange_decide(in, pr, req, &CUR, fp, fk, adj, 8, edge);
}

void test_explore_swv(void) {
    ps_profile_t pr; profile_default(&pr);
    ps_params_t req = swv(-400, 500, 50, 2), out;
    autorange_explore_params(&req, &pr, &out);
    TEST_ASSERT_EQUAL(PS_TEC_SWV, out.tec);
    TEST_ASSERT_EQUAL_INT16(-550, out.p.swv.start);
    TEST_ASSERT_EQUAL_INT16(550, out.p.swv.end);
    TEST_ASSERT_EQUAL_INT16(8, out.p.swv.step);
    TEST_ASSERT_EQUAL_INT16(50, out.p.swv.amp);
}
void test_saturation_pga(void) {
    ps_profile_t pr; profile_default(&pr);
    ps_params_t req = swv(-400, 500, 50, 2), fp; ps_gain_knobs_t fk; ps_adj_t adj[8]; bool edge;
    autorange_input_t in = {32000, false, 0, 0, 0, false};
    TEST_ASSERT_EQUAL(1, decide(&in, &pr, &req, &fp, &fk, adj, &edge));
    TEST_ASSERT_EQUAL_UINT8(3, fk.gain_idx);
    TEST_ASSERT_EQUAL_UINT8(1, fk.pga_idx);
    TEST_ASSERT_EQUAL(PS_RULE_SATURATION, adj[0].rule);
    TEST_ASSERT_EQUAL_INT32(3, adj[0].b0); TEST_ASSERT_EQUAL_INT32(2, adj[0].b1);
    TEST_ASSERT_EQUAL_INT32(3, adj[0].a0); TEST_ASSERT_EQUAL_INT32(1, adj[0].a1);
}
void test_saturation_gain(void) {
    ps_profile_t pr; profile_default(&pr);
    ps_params_t req = swv(-400, 500, 50, 2), fp; ps_gain_knobs_t fk, cur = {3, 0}; ps_adj_t adj[8]; bool edge;
    autorange_input_t in = {32000, false, 0, 0, 0, false};
    TEST_ASSERT_EQUAL(1, autorange_decide(&in, &pr, &req, &cur, &fp, &fk, adj, 8, &edge));
    TEST_ASSERT_EQUAL_UINT8(2, fk.gain_idx);
    TEST_ASSERT_EQUAL_UINT8(0, fk.pga_idx);
}
void test_small_signal_pga_then_stop(void) {
    ps_profile_t pr; profile_default(&pr);
    ps_params_t req = swv(-400, 500, 50, 2), fp; ps_gain_knobs_t fk; ps_adj_t adj[8]; bool edge;
    autorange_input_t in = in_of(0.05f, true, 0.05f);
    decide(&in, &pr, &req, &fp, &fk, adj, &edge);
    TEST_ASSERT_EQUAL_UINT8(5, fk.pga_idx);
    TEST_ASSERT_EQUAL_UINT8(3, fk.gain_idx);
    TEST_ASSERT_EQUAL(PS_RULE_SMALL, adj[0].rule);
}
void test_small_signal_limited_by_total(void) {
    ps_profile_t pr; profile_default(&pr);
    ps_params_t req = swv(-400, 500, 50, 2), fp; ps_gain_knobs_t fk; ps_adj_t adj[8]; bool edge;
    autorange_input_t in = in_of(0.30f, true, 0.05f);
    decide(&in, &pr, &req, &fp, &fk, adj, &edge);
    TEST_ASSERT_EQUAL_UINT8(3, fk.pga_idx);
    TEST_ASSERT_EQUAL_UINT8(3, fk.gain_idx);
}
void test_small_signal_raises_gain_after_pga(void) {
    ps_profile_t pr; profile_default(&pr);
    ps_params_t req = swv(-400, 500, 50, 2), fp; ps_gain_knobs_t fk; ps_adj_t adj[8]; bool edge;
    autorange_input_t in = in_of(0.01f, true, 0.01f);   /* ×8 (PGA) ×2,5 ×3,43 = 0,68 ≤ 0,8; el gain 6 daría 2,0 */
    decide(&in, &pr, &req, &fp, &fk, adj, &edge);
    TEST_ASSERT_EQUAL_UINT8(5, fk.pga_idx);
    TEST_ASSERT_EQUAL_UINT8(5, fk.gain_idx);
}
void test_zoom_with_near_baselines(void) {
    ps_profile_t pr; profile_default(&pr);
    pr.base_win1 = (ps_win_t){-100, -50}; pr.base_win2 = (ps_win_t){250, 300};
    ps_params_t req = swv(-400, 500, 50, 2), fp; ps_gain_knobs_t fk; ps_adj_t adj[8]; bool edge;
    autorange_input_t in = in_of(0.5f, true, 0.5f);
    TEST_ASSERT_EQUAL(1, decide(&in, &pr, &req, &fp, &fk, adj, &edge));
    TEST_ASSERT_EQUAL_INT16(-100, fp.p.swv.start);
    TEST_ASSERT_EQUAL_INT16(300, fp.p.swv.end);
    TEST_ASSERT_EQUAL_INT16(2, fp.p.swv.step);
    TEST_ASSERT_FALSE(edge);
    TEST_ASSERT_EQUAL(PS_RULE_ZOOM, adj[0].rule);
    TEST_ASSERT_EQUAL_INT32(-400, adj[0].b0); TEST_ASSERT_EQUAL_INT32(500, adj[0].b1);
    TEST_ASSERT_EQUAL_INT32(-100, adj[0].a0); TEST_ASSERT_EQUAL_INT32(300, adj[0].a1);
}
void test_edge_keeps_window(void) {
    ps_profile_t pr; profile_default(&pr);
    ps_params_t req = swv(-400, 500, 50, 2), fp; ps_gain_knobs_t fk; ps_adj_t adj[8]; bool edge = false;
    autorange_input_t in = in_of(0.5f, true, 0.5f); in.at_edge = true;
    TEST_ASSERT_EQUAL(1, decide(&in, &pr, &req, &fp, &fk, adj, &edge));
    TEST_ASSERT_TRUE(edge);
    TEST_ASSERT_EQUAL_INT16(-550, fp.p.swv.start);
    TEST_ASSERT_EQUAL_INT16(550, fp.p.swv.end);
    TEST_ASSERT_EQUAL_INT16(2, fp.p.swv.step);
    TEST_ASSERT_EQUAL(PS_RULE_EDGE, adj[0].rule);
}
void test_no_peak_keeps_requested(void) {
    ps_profile_t pr; profile_default(&pr);
    ps_params_t req = swv(-400, 500, 50, 2), fp; ps_gain_knobs_t fk; ps_adj_t adj[8]; bool edge;
    autorange_input_t in = {32000, false, 0, 0, 0, false};
    TEST_ASSERT_EQUAL(1, decide(&in, &pr, &req, &fp, &fk, adj, &edge));
    TEST_ASSERT_EQUAL_INT16(-400, fp.p.swv.start);
    TEST_ASSERT_EQUAL_INT16(500, fp.p.swv.end);
    TEST_ASSERT_EQUAL_INT16(2, fp.p.swv.step);
    TEST_ASSERT_EQUAL_UINT8(1, fk.pga_idx);
}
void test_ca_gain_only(void) {
    ps_profile_t pr; profile_default(&pr);
    ps_params_t req = {.tec = PS_TEC_CA}, fp, out; ps_gain_knobs_t fk; ps_adj_t adj[8]; bool edge;
    req.p.ca = (ps_ca_t){0, -200, 300, 1000, 1000, 1000, 10};
    autorange_input_t in = in_of(0.98f, true, 0.5f); in.at_edge = true;
    TEST_ASSERT_EQUAL(1, decide(&in, &pr, &req, &fp, &fk, adj, &edge));
    TEST_ASSERT_EQUAL_MEMORY(&req.p.ca, &fp.p.ca, sizeof req.p.ca);
    TEST_ASSERT_EQUAL_UINT8(1, fk.pga_idx);
    TEST_ASSERT_FALSE(edge);
    autorange_explore_params(&req, &pr, &out);
    TEST_ASSERT_EQUAL_MEMORY(&req.p.ca, &out.p.ca, sizeof req.p.ca);
}
void test_ext_gain_only_moves_pga(void) {
    ps_profile_t pr; profile_default(&pr); pr.gain_mask = 0xFF; pr.r_ext_ohm = 10000;
    ps_params_t req = swv(-400, 500, 50, 2), fp; ps_gain_knobs_t fk, cur = {PS_GAIN_EXT, 0}; ps_adj_t adj[8]; bool edge;
    autorange_input_t in = {32000, false, 0, 0, 0, false};
    TEST_ASSERT_EQUAL(0, autorange_decide(&in, &pr, &req, &cur, &fp, &fk, adj, 8, &edge));
    TEST_ASSERT_EQUAL_UINT8(PS_GAIN_EXT, fk.gain_idx);
    TEST_ASSERT_EQUAL_UINT8(0, fk.pga_idx);
}
void test_step_budget(void) {
    ps_profile_t pr; profile_default(&pr); pr.max_points = 200;
    ps_params_t req = swv(-500, 500, 25, 1), fp; ps_gain_knobs_t fk; ps_adj_t adj[8]; bool edge;
    autorange_input_t in = {1000, false, 0, 0, 0, false};
    TEST_ASSERT_EQUAL(1, decide(&in, &pr, &req, &fp, &fk, adj, &edge));
    TEST_ASSERT_EQUAL_INT16(11, fp.p.swv.step);
    TEST_ASSERT_EQUAL(PS_RULE_STEP_BUDGET, adj[0].rule);
    TEST_ASSERT_EQUAL_INT32(1, adj[0].b0); TEST_ASSERT_EQUAL_INT32(11, adj[0].a0);
    TEST_ASSERT_TRUE(tech_num_samples(&fp) <= 200u);
}
void test_explore_and_final_pass_validation(void) {
    ps_profile_t pr; ps_err_t e; profile_default(&pr);
    ps_params_t reqs[3] = { swv(-400, 500, 50, 2), dpv(-400, 500, 50, 5), npv(-400, 500, 25) };
    for (int i = 0; i < 3; i++) {
        ps_params_t ex, fp; ps_gain_knobs_t fk; ps_adj_t adj[8]; bool edge;
        TEST_ASSERT_TRUE(profile_validate_run(&pr, &reqs[i], CUR.gain_idx, 3300, &e));
        autorange_explore_params(&reqs[i], &pr, &ex);
        TEST_ASSERT_TRUE_MESSAGE(profile_validate_run(&pr, &ex, CUR.gain_idx, 3300, &e), e.field);
        autorange_input_t in = in_of(0.5f, true, 0.5f);
        decide(&in, &pr, &reqs[i], &fp, &fk, adj, &edge);
        TEST_ASSERT_TRUE_MESSAGE(profile_validate_run(&pr, &fp, fk.gain_idx, 3300, &e), e.field);
        in.at_edge = true;
        decide(&in, &pr, &reqs[i], &fp, &fk, adj, &edge);
        TEST_ASSERT_TRUE_MESSAGE(profile_validate_run(&pr, &fp, fk.gain_idx, 3300, &e), e.field);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_explore_swv);
    RUN_TEST(test_saturation_pga);
    RUN_TEST(test_saturation_gain);
    RUN_TEST(test_small_signal_pga_then_stop);
    RUN_TEST(test_small_signal_limited_by_total);
    RUN_TEST(test_small_signal_raises_gain_after_pga);
    RUN_TEST(test_zoom_with_near_baselines);
    RUN_TEST(test_edge_keeps_window);
    RUN_TEST(test_no_peak_keeps_requested);
    RUN_TEST(test_ca_gain_only);
    RUN_TEST(test_ext_gain_only_moves_pga);
    RUN_TEST(test_step_budget);
    RUN_TEST(test_explore_and_final_pass_validation);
    return UNITY_END();
}
