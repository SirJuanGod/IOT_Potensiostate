#include <unity.h>
#include "hwregs.h"

void setUp(void) {}
void tearDown(void) {}

void test_tiacn(void){
  TEST_ASSERT_EQUAL_HEX8(0x10, lmp_regs_tiacn(3,0));
  TEST_ASSERT_EQUAL_HEX8(0x03, lmp_regs_tiacn(7,3));  /* externa */
  TEST_ASSERT_EQUAL_HEX8(0x1C, lmp_regs_tiacn(6,0)); }
void test_refcn(void){
  TEST_ASSERT_EQUAL_HEX8(0xBD, lmp_regs_refcn(1,13));
  TEST_ASSERT_EQUAL_HEX8(0xA0, lmp_regs_refcn(-1,0)); }
void test_ads_config(void){
  TEST_ASSERT_EQUAL_HEX16(0x85E0, ads_config_word(2,7));
  TEST_ASSERT_EQUAL_HEX16(0x8100, ads_config_word(0,0)); }
void test_modecn(void){ TEST_ASSERT_EQUAL_HEX8(0x03, LMP_MODECN_3LEAD); }

int main(void){ UNITY_BEGIN(); RUN_TEST(test_tiacn); RUN_TEST(test_refcn); RUN_TEST(test_ads_config); RUN_TEST(test_modecn); return UNITY_END(); }
