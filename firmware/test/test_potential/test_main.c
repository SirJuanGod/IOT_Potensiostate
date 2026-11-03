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

void test_bias_changes_few(void){ /* mayor VREF dentro de 10 uV del mejor: el bias casi no salta */
  ps_bias_t b; int changes=0, prev=-1;
  for(int e=15;e<=790;e++){ TEST_ASSERT_TRUE(potential_solve(e,3300,&b)); if(prev>=0 && b.bias_idx!=prev) changes++; prev=b.bias_idx; }
  TEST_ASSERT_LESS_OR_EQUAL_INT(20, changes); }
/* potencial intermedio (uV) entre las dos escrituras, según el orden elegido */
static uint64_t mid_uv(const ps_bias_t *a, const ps_bias_t *b){
  bool dac=potential_dac_first(a,b); const ps_bias_t *pb = dac? a : b, *pv = dac? b : a;
  return (uint64_t)pv->dac_code*3300000/65536*PS_BIAS_PCT[pb->bias_idx]/100; }
void test_write_order_case(void){ ps_bias_t a,b; /* -263 -> -337: bias primero dejaría 12 % x 3,29 V = -394 mV */
  potential_solve(-263,3300,&a); potential_solve(-337,3300,&b);
  TEST_ASSERT_TRUE(potential_dac_first(&a,&b)); TEST_ASSERT_TRUE(mid_uv(&a,&b) <= 337000);
  potential_solve(-337,3300,&a); potential_solve(-263,3300,&b);   /* de vuelta: bias primero */
  TEST_ASSERT_FALSE(potential_dac_first(&a,&b)); TEST_ASSERT_TRUE(mid_uv(&a,&b) <= 337000);
  ps_bias_t z; potential_solve(0,3300,&z);   /* desde el estado seguro (bias 0): primero el DAC */
  TEST_ASSERT_TRUE(potential_dac_first(&z,&b)); TEST_ASSERT_EQUAL_UINT64(0, mid_uv(&z,&b)); }
void test_write_order_never_overshoots(void){ /* el intermedio nunca supera al mayor de los dos extremos */
  int d[3]={1,100,-200}; ps_bias_t a,b;
  for(int e=-790;e<=790;e++) for(int k=0;k<3;k++){ int f=e+d[k]; if(f<-790||f>790) continue;
    potential_solve(e,3300,&a); potential_solve(f,3300,&b);
    uint64_t lim = (uint64_t)(abs(a.e_apl_uv)>abs(b.e_apl_uv)?abs(a.e_apl_uv):abs(b.e_apl_uv));
    TEST_ASSERT_TRUE(mid_uv(&a,&b) <= lim); } }

int main(void){ UNITY_BEGIN(); RUN_TEST(test_exact_window); RUN_TEST(test_15mv_min); RUN_TEST(test_snap_zone);
  RUN_TEST(test_limits_3300); RUN_TEST(test_vref_3000_limits); RUN_TEST(test_zero_vref);
  RUN_TEST(test_bias_changes_few); RUN_TEST(test_write_order_case); RUN_TEST(test_write_order_never_overshoots); return UNITY_END(); }
