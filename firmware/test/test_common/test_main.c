#include <string.h>
#include <unity.h>
#include "ps_common.h"

void setUp(void) {}
void tearDown(void) {}

void test_pick_dr(void){ TEST_ASSERT_EQUAL(0, ps_pick_dr(200000)); TEST_ASSERT_EQUAL(4, ps_pick_dr(10000)); TEST_ASSERT_EQUAL(-1, ps_pick_dr(1000)); }
void test_conv_us(void){ TEST_ASSERT_EQUAL_UINT32(1280, ps_adc_conv_us(7)); TEST_ASSERT_EQUAL_UINT32(137500, ps_adc_conv_us(0)); }
void test_current(void){ TEST_ASSERT_FLOAT_WITHIN(0.01f, 73142.857f, ps_current_na(16384, 2, 14000, 0, 1));
                         TEST_ASSERT_FLOAT_WITHIN(0.01f, (73142.857f-10)*2, ps_current_na(16384, 2, 14000, 10, 2)); }
void test_r_tia(void){ TEST_ASSERT_EQUAL_UINT32(350000, ps_r_tia_ohm(6, 0)); TEST_ASSERT_EQUAL_UINT32(2200000, ps_r_tia_ohm(7, 2200000)); }
void test_fs_frac(void){ TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.5f, ps_na_to_fs_frac(73142.857f, 2, 14000)); }
void test_min_step(void){ TEST_ASSERT_EQUAL_UINT32(1000 + 300 + 1280 + 200, ps_min_sample_step_us(1000)); }
void test_cal_default(void){ ps_cal_t c; ps_cal_default(&c);
                             for (int i = 0; i < 8; i++) { TEST_ASSERT_EQUAL_FLOAT(0.0f, c.offset_na[i]); TEST_ASSERT_EQUAL_FLOAT(1.0f, c.gain[i]); } }
void test_err_set(void){ ps_err_t e; ps_err_set(&e, PS_ERR_RANGE, "a_very_long_field_name_exceeding_buffer", 42, "msg");
                         TEST_ASSERT_EQUAL(PS_ERR_RANGE, e.code); TEST_ASSERT_EQUAL_INT32(42, e.limit);
                         TEST_ASSERT_EQUAL_UINT(23, strlen(e.field)); TEST_ASSERT_EQUAL_STRING("msg", e.msg);
                         ps_err_set(NULL, PS_ERR_HW, "x", 0, "y"); /* no debe fallar */ }

void test_tia_frac(void){ /* holgura = min(VREF/2, 3300 − VREF/2) − 50 mV */
  /* VREF ≈ 2000 mV (código 39719): holgura 950 mV; PGA 2: 16 cuentas por mV */
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, ps_tia_frac(15200, 2, 39719, 3300));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.5f, ps_tia_frac(-7600, 2, 39719, 3300));
  /* VREF ≈ 3300 mV (código 65535): holgura 1600 mV; PGA 1: 8 cuentas por mV */
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, ps_tia_frac(12800, 1, 65535, 3300));
  TEST_ASSERT_EQUAL_HEX8(0x02, PS_FLAG_TIA_CLIP); }

int main(void){ UNITY_BEGIN(); RUN_TEST(test_pick_dr); RUN_TEST(test_conv_us); RUN_TEST(test_current);
                RUN_TEST(test_r_tia); RUN_TEST(test_fs_frac); RUN_TEST(test_min_step); RUN_TEST(test_cal_default);
                RUN_TEST(test_err_set); RUN_TEST(test_tia_frac); return UNITY_END(); }
