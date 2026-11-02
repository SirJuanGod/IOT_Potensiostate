#include <stdlib.h>
#include <unity.h>
#include "potential.h"

void setUp(void) {}
void tearDown(void) {}

void test_exact_window(void){ /* e de -700 a 700 en pasos de 1 mV, dac_vref 3300; omitir |e|<15 */
  ps_bias_t b; for(int e=-700;e<=700;e++){ if(abs(e)<15) continue;
    TEST_ASSERT_TRUE(potential_solve(e,3300,&b)); TEST_ASSERT_INT32_WITHIN(10, e*1000, b.e_apl_uv);
    TEST_ASSERT_EQUAL_INT8(e>0?1:-1, b.sign); TEST_ASSERT_TRUE(b.bias_idx>=1 && b.bias_idx<=13);
    uint32_t vref = (uint64_t)b.dac_code*3300000/65536; TEST_ASSERT_TRUE(vref>=1500000 && vref<=3300000); } }
void test_15mv_min(void){ ps_bias_t b; TEST_ASSERT_TRUE(potential_solve(15,3300,&b)); TEST_ASSERT_EQUAL_UINT8(1,b.bias_idx);
  TEST_ASSERT_TRUE((uint64_t)b.dac_code*3300000/65536 >= 1500000); TEST_ASSERT_FALSE(b.snapped); }
void test_snap_zone(void){ ps_bias_t b;
  potential_solve(0,3300,&b);  TEST_ASSERT_EQUAL_UINT8(0,b.bias_idx); TEST_ASSERT_EQUAL_INT32(0,b.e_apl_uv); TEST_ASSERT_FALSE(b.snapped);
  potential_solve(7,3300,&b);  TEST_ASSERT_EQUAL_INT32(0,b.e_apl_uv); TEST_ASSERT_TRUE(b.snapped);
  potential_solve(-8,3300,&b); TEST_ASSERT_INT32_WITHIN(10,-15000,b.e_apl_uv); TEST_ASSERT_TRUE(b.snapped); }
void test_limits_3300(void){ ps_bias_t b; TEST_ASSERT_TRUE(potential_solve(791,3300,&b)); TEST_ASSERT_FALSE(potential_solve(792,3300,&b)); TEST_ASSERT_FALSE(potential_solve(-792,3300,&b)); }
void test_vref_3000_limits(void){ ps_bias_t b; TEST_ASSERT_TRUE(potential_solve(719,3000,&b)); TEST_ASSERT_FALSE(potential_solve(720,3000,&b));
  TEST_ASSERT_INT32_WITHIN(10, 500000, (potential_solve(500,3000,&b), b.e_apl_uv)); }
void test_zero_vref(void){ ps_bias_t b; potential_solve(0,3300,&b); TEST_ASSERT_INT32_WITHIN(100000, 2000000, (int32_t)((uint64_t)b.dac_code*3300000/65536)); }

int main(void){ UNITY_BEGIN(); RUN_TEST(test_exact_window); RUN_TEST(test_15mv_min); RUN_TEST(test_snap_zone);
  RUN_TEST(test_limits_3300); RUN_TEST(test_vref_3000_limits); RUN_TEST(test_zero_vref); return UNITY_END(); }
