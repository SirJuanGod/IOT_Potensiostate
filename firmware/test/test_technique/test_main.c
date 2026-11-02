#include <unity.h>
#include "technique.h"

void setUp(void) {}
void tearDown(void) {}

static ps_params_t swv(int s,int e){ ps_params_t p={.tec=PS_TEC_SWV}; p.p.swv=(ps_swv_t){s,e,50,1,25.0f}; return p; }
void test_swv_forward(void){ ps_params_t p=swv(-400,500); ps_step_t st;
  TEST_ASSERT_EQUAL_UINT32(1802, tech_num_steps(&p)); TEST_ASSERT_EQUAL_UINT32(1802, tech_num_samples(&p));
  tech_step_at(&p,0,&st); TEST_ASSERT_EQUAL_INT16(-350,st.e_mv); TEST_ASSERT_EQUAL_INT16(-400,st.x_mv); TEST_ASSERT_EQUAL_UINT32(20000,st.dur_us); TEST_ASSERT_EQUAL(PS_TAG_FWD,st.tag);
  tech_step_at(&p,1,&st); TEST_ASSERT_EQUAL_INT16(-450,st.e_mv); TEST_ASSERT_EQUAL(PS_TAG_REV,st.tag);
  tech_step_at(&p,1801,&st); TEST_ASSERT_EQUAL_INT16(500,st.x_mv); TEST_ASSERT_EQUAL_INT16(450,st.e_mv);
  TEST_ASSERT_FALSE(tech_step_at(&p,1802,&st)); }
void test_swv_backward(void){ ps_params_t p=swv(500,-400); ps_step_t st; tech_step_at(&p,0,&st);
  TEST_ASSERT_EQUAL_INT16(500,st.x_mv); TEST_ASSERT_EQUAL_INT16(550,st.e_mv); tech_step_at(&p,2,&st); TEST_ASSERT_EQUAL_INT16(499,st.x_mv); }
void test_dpv(void){ /* 0→100, amp 50, step 10, period 200, width 50 */
  ps_params_t p={.tec=PS_TEC_DPV}; p.p.dpv=(ps_dpv_t){0,100,50,10,200,50}; ps_step_t st;
  TEST_ASSERT_EQUAL_UINT32(22, tech_num_steps(&p)); TEST_ASSERT_EQUAL_UINT32(22, tech_num_samples(&p));
  tech_step_at(&p,0,&st); TEST_ASSERT_EQUAL_INT16(0,st.e_mv); TEST_ASSERT_EQUAL_INT16(0,st.x_mv); TEST_ASSERT_EQUAL_UINT32(150000,st.dur_us); TEST_ASSERT_EQUAL(PS_TAG_BASE,st.tag);
  tech_step_at(&p,1,&st); TEST_ASSERT_EQUAL_INT16(50,st.e_mv); TEST_ASSERT_EQUAL_INT16(0,st.x_mv); TEST_ASSERT_EQUAL_UINT32(50000,st.dur_us); TEST_ASSERT_EQUAL(PS_TAG_PULSE,st.tag);
  tech_step_at(&p,21,&st); TEST_ASSERT_EQUAL_INT16(150,st.e_mv); TEST_ASSERT_EQUAL_INT16(100,st.x_mv);
  TEST_ASSERT_FALSE(tech_step_at(&p,22,&st)); }
void test_npv(void){ /* -200→0, amp 50, width 50, period 200, quiet 500 */
  ps_params_t p={.tec=PS_TEC_NPV}; p.p.npv=(ps_npv_t){-200,0,50,50,200,500}; ps_step_t st;
  TEST_ASSERT_EQUAL_UINT32(11, tech_num_steps(&p)); TEST_ASSERT_EQUAL_UINT32(10, tech_num_samples(&p));
  tech_step_at(&p,0,&st); TEST_ASSERT_EQUAL_INT16(-200,st.e_mv); TEST_ASSERT_EQUAL_UINT32(500000,st.dur_us); TEST_ASSERT_EQUAL(PS_TAG_QUIET,st.tag); TEST_ASSERT_FALSE(st.sample);
  tech_step_at(&p,1,&st); TEST_ASSERT_EQUAL_INT16(-200,st.e_mv); TEST_ASSERT_EQUAL(PS_TAG_PULSE,st.tag); TEST_ASSERT_EQUAL_UINT32(50000,st.dur_us); TEST_ASSERT_TRUE(st.sample);
  tech_step_at(&p,2,&st); TEST_ASSERT_EQUAL_INT16(-200,st.e_mv); TEST_ASSERT_EQUAL(PS_TAG_BASE,st.tag); TEST_ASSERT_EQUAL_UINT32(150000,st.dur_us);
  tech_step_at(&p,3,&st); TEST_ASSERT_EQUAL_INT16(-150,st.e_mv); TEST_ASSERT_EQUAL(PS_TAG_PULSE,st.tag);
  tech_step_at(&p,10,&st); TEST_ASSERT_EQUAL_INT16(-200,st.e_mv); TEST_ASSERT_EQUAL(PS_TAG_BASE,st.tag);
  TEST_ASSERT_FALSE(tech_step_at(&p,11,&st)); }
void test_cv_two_cycles(void){ /* cycles 2, start 0, end 0, v1 450, v2 -300, step 5, rate 50 */
  ps_params_t p={.tec=PS_TEC_CV}; p.p.cv=(ps_cv_t){2,0,0,450,-300,5,50}; ps_step_t st;
  TEST_ASSERT_EQUAL_UINT32(601, tech_num_steps(&p)); TEST_ASSERT_EQUAL_UINT32(601, tech_num_samples(&p));
  tech_step_at(&p,0,&st); TEST_ASSERT_EQUAL_INT16(0,st.e_mv); TEST_ASSERT_EQUAL_UINT32(100000,st.dur_us); TEST_ASSERT_EQUAL(PS_TAG_STEP,st.tag);
  tech_step_at(&p,90,&st); TEST_ASSERT_EQUAL_INT16(450,st.e_mv); TEST_ASSERT_EQUAL_INT16(450,st.x_mv); TEST_ASSERT_EQUAL(PS_BR_UP,st.branch); TEST_ASSERT_EQUAL_UINT8(0,st.cycle);
  tech_step_at(&p,91,&st); TEST_ASSERT_EQUAL_INT16(445,st.e_mv); TEST_ASSERT_EQUAL(PS_BR_DOWN,st.branch);
  tech_step_at(&p,300,&st); TEST_ASSERT_EQUAL_INT16(0,st.e_mv); TEST_ASSERT_EQUAL(PS_BR_UP,st.branch); TEST_ASSERT_EQUAL_UINT8(0,st.cycle);
  tech_step_at(&p,301,&st); TEST_ASSERT_EQUAL_INT16(5,st.e_mv); TEST_ASSERT_EQUAL(PS_BR_UP,st.branch); TEST_ASSERT_EQUAL_UINT8(1,st.cycle);
  tech_step_at(&p,600,&st); TEST_ASSERT_EQUAL_INT16(0,st.e_mv); TEST_ASSERT_EQUAL_UINT8(1,st.cycle);
  TEST_ASSERT_FALSE(tech_step_at(&p,601,&st)); }
void test_cv_starts_down(void){ /* start 0, v1 -300, v2 450, end 0, step 5, 1 ciclo */
  ps_params_t p={.tec=PS_TEC_CV}; p.p.cv=(ps_cv_t){1,0,0,-300,450,5,50}; ps_step_t st;
  tech_step_at(&p,1,&st); TEST_ASSERT_EQUAL_INT16(-5,st.e_mv); TEST_ASSERT_EQUAL(PS_BR_DOWN,st.branch);
  tech_step_at(&p,60,&st); TEST_ASSERT_EQUAL_INT16(-300,st.e_mv);
  tech_step_at(&p,61,&st); TEST_ASSERT_EQUAL_INT16(-295,st.e_mv); TEST_ASSERT_EQUAL(PS_BR_UP,st.branch); }
void test_ca(void){ /* pre 0, quiet 1000, v1 -200, t1 2000, v2 200, t2 2000, samples 10 */
  ps_params_t p={.tec=PS_TEC_CA}; p.p.ca=(ps_ca_t){0,-200,200,1000,2000,2000,10}; ps_step_t st;
  TEST_ASSERT_EQUAL_UINT32(30, tech_num_steps(&p)); TEST_ASSERT_EQUAL_UINT32(30, tech_num_samples(&p));
  tech_step_at(&p,0,&st); TEST_ASSERT_EQUAL_INT16(0,st.e_mv); TEST_ASSERT_EQUAL_INT16(0,st.x_mv); TEST_ASSERT_EQUAL_UINT32(100000,st.dur_us); TEST_ASSERT_EQUAL_UINT8(0,st.branch); TEST_ASSERT_TRUE(st.sample); TEST_ASSERT_EQUAL(PS_TAG_STEP,st.tag);
  tech_step_at(&p,10,&st); TEST_ASSERT_EQUAL_INT16(-200,st.e_mv); TEST_ASSERT_EQUAL_INT16(-200,st.x_mv); TEST_ASSERT_EQUAL_UINT32(200000,st.dur_us); TEST_ASSERT_EQUAL_UINT8(1,st.branch);
  tech_step_at(&p,29,&st); TEST_ASSERT_EQUAL_INT16(200,st.e_mv); TEST_ASSERT_EQUAL_UINT8(2,st.branch);
  TEST_ASSERT_FALSE(tech_step_at(&p,30,&st)); }
void test_extremes(void){ ps_params_t p=swv(-400,500); int16_t lo,hi; tech_e_extremes(&p,&lo,&hi);
  TEST_ASSERT_EQUAL_INT16(-450,lo); TEST_ASSERT_EQUAL_INT16(550,hi);
  p.tec=PS_TEC_CV; p.p.cv=(ps_cv_t){1,0,0,450,-300,5,50}; tech_e_extremes(&p,&lo,&hi);
  TEST_ASSERT_EQUAL_INT16(-300,lo); TEST_ASSERT_EQUAL_INT16(450,hi); }
void test_window_roundtrip(void){ ps_params_t p=swv(-400,500); int16_t lo,hi,step;
  TEST_ASSERT_TRUE(tech_has_window(PS_TEC_SWV)); TEST_ASSERT_FALSE(tech_has_window(PS_TEC_CV)); TEST_ASSERT_FALSE(tech_has_window(PS_TEC_CA));
  tech_set_window(&p,-100,300,2);
  TEST_ASSERT_EQUAL_INT16(-100,p.p.swv.start); TEST_ASSERT_EQUAL_INT16(300,p.p.swv.end); TEST_ASSERT_EQUAL_INT16(2,p.p.swv.step);
  tech_get_window(&p,&lo,&hi,&step); TEST_ASSERT_EQUAL_INT16(-100,lo); TEST_ASSERT_EQUAL_INT16(300,hi); TEST_ASSERT_EQUAL_INT16(2,step);
  p=swv(500,-400); tech_set_window(&p,-100,300,2);
  TEST_ASSERT_EQUAL_INT16(300,p.p.swv.start); TEST_ASSERT_EQUAL_INT16(-100,p.p.swv.end);
  tech_get_window(&p,&lo,&hi,&step); TEST_ASSERT_EQUAL_INT16(-100,lo); TEST_ASSERT_EQUAL_INT16(300,hi); }

int main(void){ UNITY_BEGIN(); RUN_TEST(test_swv_forward); RUN_TEST(test_swv_backward); RUN_TEST(test_dpv); RUN_TEST(test_npv);
  RUN_TEST(test_cv_two_cycles); RUN_TEST(test_cv_starts_down); RUN_TEST(test_ca); RUN_TEST(test_extremes);
  RUN_TEST(test_window_roundtrip); return UNITY_END(); }
