#include <string.h>
#include <stdio.h>
#include <unity.h>
#include "protocol.h"
#include "cJSON.h"

/* Unity sin soporte de double: se compara en float */
#define EQ_D(a, b) TEST_ASSERT_EQUAL_FLOAT((float)(a), (float)(b))
void setUp(void) {}
void tearDown(void) {}

static bool parse(const char *s, ps_cmd_t *c, ps_err_t *e) { return proto_parse_cmd(s, strlen(s), c, e); }
static void expect_err(const char *json, ps_err_code_t code, const char *field) {
    ps_cmd_t c; ps_err_t e;
    TEST_ASSERT_FALSE_MESSAGE(parse(json, &c, &e), json);
    TEST_ASSERT_EQUAL_MESSAGE(code, e.code, json);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(field, e.field, json);
}

void test_parse_run_swv(void) {
    ps_cmd_t c; ps_err_t e;
    const char *j = "{\"cmd\":\"run\",\"id\":\"muestra_07\",\"tec\":\"swv\",\"auto\":true,"
                    "\"params\":{\"start\":-400,\"end\":500,\"amp\":50,\"step\":1,\"freq_hz\":31.25},"
                    "\"gain\":\"auto\"}";
    TEST_ASSERT_TRUE(parse(j, &c, &e));
    TEST_ASSERT_EQUAL(PS_CMD_RUN, c.kind);
    TEST_ASSERT_EQUAL_STRING("muestra_07", c.id);
    TEST_ASSERT_TRUE(c.auto_adj);
    TEST_ASSERT_TRUE(c.gain_auto);
    TEST_ASSERT_EQUAL(PS_TEC_SWV, c.params.tec);
    TEST_ASSERT_EQUAL_INT16(-400, c.params.p.swv.start);
    TEST_ASSERT_EQUAL_INT16(500, c.params.p.swv.end);
    TEST_ASSERT_EQUAL_INT16(50, c.params.p.swv.amp);
    TEST_ASSERT_EQUAL_INT16(1, c.params.p.swv.step);
    TEST_ASSERT_EQUAL_FLOAT(31.25f, c.params.p.swv.freq_hz);
}
void test_parse_run_gain_and_defaults(void) {
    ps_cmd_t c; ps_err_t e;
    const char *j = "{\"cmd\":\"run\",\"id\":\"a\",\"tec\":\"ca\",\"gain\":5,\"params\":{\"pre_step\":0,\"quiet_ms\":100,"
                    "\"v1\":-200,\"t1_ms\":500,\"v2\":300,\"t2_ms\":500,\"samples\":20}}";
    TEST_ASSERT_TRUE(parse(j, &c, &e));
    TEST_ASSERT_FALSE(c.auto_adj);
    TEST_ASSERT_FALSE(c.gain_auto);
    TEST_ASSERT_EQUAL_UINT8(5, c.gain_idx);
    TEST_ASSERT_EQUAL_UINT16(20, c.params.p.ca.samples);
    TEST_ASSERT_EQUAL_UINT32(500, c.params.p.ca.t2_ms);
}
void test_parse_stop(void) {
    ps_cmd_t c; ps_err_t e;
    TEST_ASSERT_TRUE(parse("{\"cmd\":\"stop\"}", &c, &e));
    TEST_ASSERT_EQUAL(PS_CMD_STOP, c.kind);
}
void test_parse_simple_cmds(void) {
    ps_cmd_t c; ps_err_t e;
    TEST_ASSERT_TRUE(parse("{\"cmd\":\"get_profile\"}", &c, &e)); TEST_ASSERT_EQUAL(PS_CMD_GET_PROFILE, c.kind);
    TEST_ASSERT_TRUE(parse("{\"cmd\":\"get_calibration\"}", &c, &e)); TEST_ASSERT_EQUAL(PS_CMD_GET_CAL, c.kind);
    TEST_ASSERT_TRUE(parse("{\"cmd\":\"select_profile\",\"name\":\"cafeina\"}", &c, &e));
    TEST_ASSERT_EQUAL(PS_CMD_SELECT_PROFILE, c.kind);
    TEST_ASSERT_EQUAL_STRING("cafeina", c.name);
}
void test_parse_errors(void) {
    expect_err("{bad", PS_ERR_PARSE, "");
    expect_err("{\"cmd\":\"stop\"} basura", PS_ERR_PARSE, "");
    expect_err("{\"cmd\":\"stop\",\"x\":[[[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]]]}", PS_ERR_PARSE, "");   /* anidamiento > 16 */
    expect_err("{\"cmd\":\"run\",\"tec\":\"swv\",\"params\":{}}", PS_ERR_MISSING, "id");
    expect_err("{\"cmd\":\"run\",\"id\":\"a\",\"tec\":\"eis\",\"params\":{}}", PS_ERR_RANGE, "tec");
    expect_err("{\"cmd\":\"run\",\"id\":\"a\",\"tec\":\"swv\",\"params\":{\"start\":\"x\",\"end\":500,\"amp\":50,\"step\":1,\"freq_hz\":25}}",
               PS_ERR_TYPE, "start");
    expect_err("{\"cmd\":\"run\",\"id\":\"a\",\"tec\":\"swv\",\"gain\":9,\"params\":{\"start\":0,\"end\":500,\"amp\":50,\"step\":1,\"freq_hz\":25}}",
               PS_ERR_RANGE, "gain");
    expect_err("{\"cmd\":\"explota\"}", PS_ERR_RANGE, "cmd");
    expect_err("{\"cmd\":\"run\",\"id\":\"a\",\"tec\":\"swv\",\"params\":{\"start\":0,\"end\":500,\"amp\":50,\"step\":1}}",
               PS_ERR_MISSING, "freq_hz");
    expect_err("{\"cmd\":\"run\",\"id\":\"\",\"tec\":\"swv\",\"params\":{}}", PS_ERR_RANGE, "id");
    expect_err("{\"cmd\":\"run\",\"id\":\"1234567890123456789012345678901x\",\"tec\":\"swv\",\"params\":{}}", PS_ERR_RANGE, "id");
}
void test_parse_rejects_out_of_type_range(void) {
    const char *pre = "{\"cmd\":\"run\",\"id\":\"a\",\"tec\":\"swv\",\"params\":";
    char j[256];
    snprintf(j, sizeof j, "%s{\"start\":66136,\"end\":500,\"amp\":50,\"step\":1,\"freq_hz\":25}}", pre);
    expect_err(j, PS_ERR_RANGE, "start");
    snprintf(j, sizeof j, "%s{\"start\":1.5,\"end\":500,\"amp\":50,\"step\":1,\"freq_hz\":25}}", pre);
    expect_err(j, PS_ERR_TYPE, "start");
    expect_err("{\"cmd\":\"run\",\"id\":\"a\",\"tec\":\"ca\",\"params\":{\"pre_step\":0,\"quiet_ms\":100,\"v1\":-200,\"t1_ms\":500,"
               "\"v2\":300,\"t2_ms\":500,\"samples\":-1}}", PS_ERR_RANGE, "samples");
    expect_err("{\"cmd\":\"run\",\"id\":\"a\",\"tec\":\"ca\",\"params\":{\"pre_step\":0,\"quiet_ms\":4294967296,\"v1\":-200,\"t1_ms\":500,"
               "\"v2\":300,\"t2_ms\":500,\"samples\":3}}", PS_ERR_RANGE, "quiet_ms");
}

void test_profile_roundtrip(void) {
    ps_profile_t p; profile_default(&p);
    char body[1500], msg[1600]; ps_cmd_t c; ps_err_t e;
    int n = proto_write_profile(body, sizeof body, &p);
    TEST_ASSERT_GREATER_THAN(0, n);
    snprintf(msg, sizeof msg, "{\"cmd\":\"set_profile\",\"profile\":%s}", body);
    TEST_ASSERT_TRUE_MESSAGE(parse(msg, &c, &e), e.field);
    TEST_ASSERT_EQUAL(PS_CMD_SET_PROFILE, c.kind);
    TEST_ASSERT_EQUAL_MEMORY(&p, &c.profile, sizeof p);
    /* un perfil no trivial: ventanas y técnica por defecto distintas */
    p.redox = -1; p.freq_min_hz = 0.1f; p.peak_win = (ps_win_t){-300, 450}; p.defaults.tec = PS_TEC_DPV;
    p.defaults.p.dpv = (ps_dpv_t){-500, 500, 50, 4, 200, 50};
    n = proto_write_profile(body, sizeof body, &p);
    snprintf(msg, sizeof msg, "{\"cmd\":\"set_profile\",\"profile\":%s}", body);
    TEST_ASSERT_TRUE(parse(msg, &c, &e));
    TEST_ASSERT_EQUAL_MEMORY(&p, &c.profile, sizeof p);
}
void test_cal_roundtrip(void) {
    ps_cal_t k; ps_cal_default(&k);
    for (int i = 0; i < 8; i++) { k.offset_na[i] = 0.1f * (float)i - 0.3f; k.gain[i] = 1.0f / (3.0f + (float)i); }
    char body[400], msg[500]; ps_cmd_t c; ps_err_t e;
    TEST_ASSERT_GREATER_THAN(0, proto_write_cal(body, sizeof body, &k));
    snprintf(msg, sizeof msg, "{\"cmd\":\"set_calibration\",\"cal\":%s}", body);
    TEST_ASSERT_TRUE(parse(msg, &c, &e));
    TEST_ASSERT_EQUAL(PS_CMD_SET_CAL, c.kind);
    TEST_ASSERT_EQUAL_MEMORY(&k, &c.cal, sizeof k);
    expect_err("{\"cmd\":\"set_calibration\",\"cal\":{\"offset_na\":[1,2],\"gain\":[1,1,1,1,1,1,1,1]}}", PS_ERR_RANGE, "offset_na");
}
void test_set_profile_missing_field(void) {
    expect_err("{\"cmd\":\"set_profile\",\"profile\":{\"name\":\"x\"}}", PS_ERR_MISSING, "e_min_mv");
    expect_err("{\"cmd\":\"set_profile\"}", PS_ERR_MISSING, "profile");
}

#define NS 500
void test_rows_chunking(void) {
    static ps_sample_t s[NS]; static float in[NS]; static char buf[4096];
    memset(s, 0, sizeof s);
    for (int i = 0; i < NS; i++) {
        s[i].t_us = 1000u * (uint32_t)i; s[i].x_mv = (int16_t)(i - 250); s[i].e_apl_uv = -1234 * i; s[i].sign = -1;
        s[i].dac_code = 40000; s[i].adc_raw = (int16_t)(-i); s[i].pga = 2; s[i].dr = 7; s[i].tag = PS_TAG_FWD;
        in[i] = 0.001f * (float)i;
    }
    size_t pos = 0, total = 0; uint32_t seq = 0; int blocks = 0; bool last = false;
    while (!last) {
        size_t used; int n = proto_write_rows(buf, sizeof buf, "m1", PS_PASS_FINAL, seq++, s + pos, in + pos, NS - pos, &used, &last);
        TEST_ASSERT_GREATER_THAN(0, n);
        TEST_ASSERT_LESS_OR_EQUAL(sizeof buf - 1, (size_t)n);
        TEST_ASSERT_EQUAL((int)strlen(buf), n);
        cJSON *root = cJSON_Parse(buf);
        TEST_ASSERT_NOT_NULL_MESSAGE(root, buf);
        cJSON *rows = cJSON_GetObjectItem(root, "rows");
        TEST_ASSERT_EQUAL((int)used, cJSON_GetArraySize(rows));
        TEST_ASSERT_EQUAL(15, cJSON_GetArraySize(cJSON_GetArrayItem(rows, 0)));
        TEST_ASSERT_EQUAL_STRING("final", cJSON_GetObjectItem(root, "pass")->valuestring);
        TEST_ASSERT_EQUAL((int)(seq - 1), cJSON_GetObjectItem(root, "seq")->valueint);
        TEST_ASSERT_EQUAL(last, cJSON_IsTrue(cJSON_GetObjectItem(root, "last")));
        cJSON_Delete(root);
        pos += used; total += used; blocks++;
        TEST_ASSERT_LESS_THAN(50, blocks);
    }
    TEST_ASSERT_EQUAL(NS, total);
    TEST_ASSERT_GREATER_THAN(1, blocks);
}
void test_rows_content_and_tiny_cap(void) {
    ps_sample_t s = {.t_us = 7, .tag = PS_TAG_PULSE, .cycle = 1, .branch = 1, .x_mv = -50, .e_set_mv = -48, .e_apl_uv = -48123,
                     .sign = -1, .bias_idx = 3, .dac_code = 1234, .adc_raw = -321, .pga = 2, .dr = 5, .flags = 1};
    float i = 12.34567f; char buf[512]; size_t used; bool last;
    TEST_ASSERT_GREATER_THAN(0, proto_write_rows(buf, sizeof buf, "x\"y", PS_PASS_EXPLORE, 0, &s, &i, 1, &used, &last));
    TEST_ASSERT_TRUE(last); TEST_ASSERT_EQUAL(1, used);
    cJSON *root = cJSON_Parse(buf); TEST_ASSERT_NOT_NULL_MESSAGE(root, buf);
    TEST_ASSERT_EQUAL_STRING("x\"y", cJSON_GetObjectItem(root, "id")->valuestring);
    cJSON *r = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "rows"), 0);
    const double ex[15] = {7, PS_TAG_PULSE, 1, 1, -50, -48, -48123, -1, 3, 1234, -321, 2, 5, 12.346, 1};
    for (int k = 0; k < 15; k++) EQ_D(ex[k], cJSON_GetArrayItem(r, k)->valuedouble);
    cJSON_Delete(root);
    TEST_ASSERT_EQUAL(-1, proto_write_rows(buf, 40, "m1", PS_PASS_FINAL, 0, &s, &i, 1, &used, &last));
}

void test_header(void) {
    ps_params_t p = {.tec = PS_TEC_SWV}; p.p.swv = (ps_swv_t){-400, 500, 50, 1, 31.25f};
    ps_run_meta_t m = {"muestra_07", PS_PASS_EXPLORE, &p, "default", 3, 0, 2, 3300, 901, 123456789012ull, "1.0.0",
                       2200000, -1.5f, 1.25f};
    char buf[1500];
    TEST_ASSERT_GREATER_THAN(0, proto_write_header(buf, sizeof buf, &m));
    cJSON *r = cJSON_Parse(buf); TEST_ASSERT_NOT_NULL_MESSAGE(r, buf);
    TEST_ASSERT_EQUAL_STRING("explore", cJSON_GetObjectItem(r, "pass")->valuestring);
    TEST_ASSERT_EQUAL_STRING("swv", cJSON_GetObjectItem(r, "tec")->valuestring);
    EQ_D(31.25, cJSON_GetObjectItem(cJSON_GetObjectItem(r, "params"), "freq_hz")->valuedouble);
    EQ_D(123456789012.0, cJSON_GetObjectItem(r, "t0_us")->valuedouble);
    EQ_D(2200000, cJSON_GetObjectItem(r, "r_ext_ohm")->valuedouble);
    EQ_D(-1.5, cJSON_GetObjectItem(r, "cal_offset_nA")->valuedouble);
    EQ_D(1.25, cJSON_GetObjectItem(r, "cal_gain")->valuedouble);
    TEST_ASSERT_EQUAL(15, cJSON_GetArraySize(cJSON_GetObjectItem(r, "cols")));
    TEST_ASSERT_EQUAL_STRING("i_nA", cJSON_GetArrayItem(cJSON_GetObjectItem(r, "cols"), 13)->valuestring);
    cJSON_Delete(r);
    TEST_ASSERT_EQUAL(-1, proto_write_header(buf, 50, &m));
}

void test_status(void) {
    char buf[200]; ps_err_t e;
    TEST_ASSERT_EQUAL_STRING("{\"state\":\"idle\"}", (proto_write_status(buf, sizeof buf, "idle", NULL), buf));
    ps_err_set(&e, PS_ERR_RANGE, "amp", 100, "supera amp_max");
    TEST_ASSERT_GREATER_THAN(0, proto_write_status(buf, sizeof buf, "error", &e));
    cJSON *r = cJSON_Parse(buf); TEST_ASSERT_NOT_NULL_MESSAGE(r, buf);
    TEST_ASSERT_EQUAL_STRING("error", cJSON_GetObjectItem(r, "state")->valuestring);
    TEST_ASSERT_EQUAL_STRING("range", cJSON_GetObjectItem(r, "code")->valuestring);
    TEST_ASSERT_EQUAL_STRING("amp", cJSON_GetObjectItem(r, "field")->valuestring);
    TEST_ASSERT_EQUAL(100, cJSON_GetObjectItem(r, "limit")->valueint);
    cJSON_Delete(r);
}

void test_result_flags(void) {
    ps_features_t f = {0}; char buf[600];
    TEST_ASSERT_GREATER_THAN(0, proto_write_result(buf, sizeof buf, "m1", PS_TEC_SWV, &f, NULL, 0, PS_RUN_NO_PEAK | PS_RUN_ABORTED));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"flags\":[\"no_peak\",\"aborted\"]"));
    cJSON *r = cJSON_Parse(buf); TEST_ASSERT_NOT_NULL_MESSAGE(r, buf);
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(r, "features")));
    TEST_ASSERT_EQUAL(0, cJSON_GetArraySize(cJSON_GetObjectItem(r, "adjustments")));
    cJSON_Delete(r);
    TEST_ASSERT_GREATER_THAN(0, proto_write_result(buf, sizeof buf, "m1", PS_TEC_CV, &f, NULL, 0, PS_RUN_PEAK_AT_EDGE | PS_RUN_ERROR));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"flags\":[\"peak_at_edge\",\"error\"]"));
}
void test_result_features_and_adjustments(void) {
    ps_features_t f = {.has_peak = true, .pk = {.base_m = 0.5f, .base_b = -2.25f, .ip_na = 120.5f, .area_na_mv = 4000.f, .fwhm_mv = 80.f, .ep_mv = -125}};
    ps_adj_t adj[2] = {{PS_RULE_SATURATION, 3, 2, 3, 3}, {PS_RULE_ZOOM, -600, 600, -300, 350}};
    char buf[800];
    TEST_ASSERT_GREATER_THAN(0, proto_write_result(buf, sizeof buf, "m1", PS_TEC_DPV, &f, adj, 2, 0));
    cJSON *r = cJSON_Parse(buf); TEST_ASSERT_NOT_NULL_MESSAGE(r, buf);
    cJSON *ft = cJSON_GetObjectItem(r, "features");
    EQ_D(120.5, cJSON_GetObjectItem(ft, "ip_nA")->valuedouble);
    EQ_D(-125, cJSON_GetObjectItem(ft, "Ep_mV")->valuedouble);
    EQ_D(-2.25, cJSON_GetObjectItem(ft, "baseline_b")->valuedouble);
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(ft, "baseline_m"));
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(ft, "area_nA_mV"));
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(ft, "fwhm_mV"));
    cJSON *a = cJSON_GetObjectItem(r, "adjustments");
    TEST_ASSERT_EQUAL(2, cJSON_GetArraySize(a));
    TEST_ASSERT_EQUAL_STRING("saturation", cJSON_GetObjectItem(cJSON_GetArrayItem(a, 0), "rule")->valuestring);
    cJSON *z = cJSON_GetArrayItem(a, 1);
    TEST_ASSERT_EQUAL_STRING("zoom", cJSON_GetObjectItem(z, "rule")->valuestring);
    TEST_ASSERT_EQUAL(-600, cJSON_GetArrayItem(cJSON_GetObjectItem(z, "before"), 0)->valueint);
    TEST_ASSERT_EQUAL(350, cJSON_GetArrayItem(cJSON_GetObjectItem(z, "after"), 1)->valueint);
    cJSON_Delete(r);
}
void test_result_ca(void) {
    ps_features_t f = {0}; f.ca = (ps_ca_feat_t){{1.5f, 2.5f, 3.5f}, {10.f, 20.f, 30.f}}; char buf[600];
    TEST_ASSERT_GREATER_THAN(0, proto_write_result(buf, sizeof buf, "m1", PS_TEC_CA, &f, NULL, 0, 0));
    cJSON *r = cJSON_Parse(buf); TEST_ASSERT_NOT_NULL_MESSAGE(r, buf);
    cJSON *ft = cJSON_GetObjectItem(r, "features");
    TEST_ASSERT_EQUAL(3, cJSON_GetArraySize(cJSON_GetObjectItem(ft, "iss_nA")));
    EQ_D(30.0, cJSON_GetArrayItem(cJSON_GetObjectItem(ft, "q_nC"), 2)->valuedouble);
    cJSON_Delete(r);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_run_swv);
    RUN_TEST(test_parse_run_gain_and_defaults);
    RUN_TEST(test_parse_stop);
    RUN_TEST(test_parse_simple_cmds);
    RUN_TEST(test_parse_errors);
    RUN_TEST(test_parse_rejects_out_of_type_range);
    RUN_TEST(test_profile_roundtrip);
    RUN_TEST(test_cal_roundtrip);
    RUN_TEST(test_set_profile_missing_field);
    RUN_TEST(test_rows_chunking);
    RUN_TEST(test_rows_content_and_tiny_cap);
    RUN_TEST(test_header);
    RUN_TEST(test_status);
    RUN_TEST(test_result_flags);
    RUN_TEST(test_result_features_and_adjustments);
    RUN_TEST(test_result_ca);
    return UNITY_END();
}
