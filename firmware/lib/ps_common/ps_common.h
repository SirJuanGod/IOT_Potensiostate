/* Tipos, constantes y utilidades puras compartidas (sin dependencias de ESP-IDF). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define PS_NUM_BIAS 14
extern const uint8_t  PS_BIAS_PCT[PS_NUM_BIAS];   /* % de VREF del LMP91000 */
#define PS_GAIN_EXT 7                              /* índice de ganancia: resistencia externa */
extern const uint32_t PS_TIA_GAIN_OHM[7];          /* ganancias internas del TIA (LMP91000) */
extern const uint16_t PS_PGA_FSR_MV[6];            /* fondo de escala del PGA del ADS1115 */
extern const uint16_t PS_DR_SPS[8];                /* data rates del ADS1115 */

#define PS_VREF_MIN_MV 1500
#define PS_VREF_MAX_MV 3300
#define PS_VREF_ZERO_MV 2000        /* VREF usado cuando E = 0 */
#define PS_PGA_DEFAULT 2
#define PS_MAX_SAMPLES 4000
#define PS_WRITE_US 300             /* I2C bias + SPI DAC, peor caso */
#define PS_ADC_MARGIN_US 200

typedef enum { PS_TEC_CV, PS_TEC_SWV, PS_TEC_DPV, PS_TEC_NPV, PS_TEC_CA } ps_tec_t;
typedef enum { PS_TAG_STEP, PS_TAG_FWD, PS_TAG_REV, PS_TAG_BASE, PS_TAG_PULSE, PS_TAG_QUIET } ps_tag_t;
enum { PS_BR_UP = 0, PS_BR_DOWN = 1 };            /* CA usa branch = segmento 0..2 */
#define PS_FLAG_SNAPPED 0x01

typedef struct { int16_t lo, hi; } ps_win_t;
typedef struct { uint8_t cycles; int16_t start, end, vertex1, vertex2, step; uint16_t rate_mv_s; } ps_cv_t;
typedef struct { int16_t start, end, amp, step; float freq_hz; } ps_swv_t;
typedef struct { int16_t start, end, amp, step; uint32_t period_ms, width_ms; } ps_dpv_t;
typedef struct { int16_t start, end, amp; uint32_t width_ms, period_ms, quiet_ms; } ps_npv_t;
typedef struct { int16_t pre_step, v1, v2; uint32_t quiet_ms, t1_ms, t2_ms; uint16_t samples; } ps_ca_t;
typedef struct { ps_tec_t tec; union { ps_cv_t cv; ps_swv_t swv; ps_dpv_t dpv; ps_npv_t npv; ps_ca_t ca; } p; } ps_params_t;
typedef struct { int16_t e_mv, x_mv; uint32_t dur_us; uint8_t tag, cycle, branch; bool sample; } ps_step_t;
typedef struct { uint32_t t_us; int16_t x_mv, e_set_mv; int32_t e_apl_uv; int8_t sign; uint8_t bias_idx;
                 uint16_t dac_code; int16_t adc_raw; uint8_t pga, dr, tag, cycle, branch, flags; } ps_sample_t;
typedef struct { float offset_na[8]; float gain[8]; } ps_cal_t;   /* por índice de ganancia 0..7 */

typedef enum { PS_ERR_NONE, PS_ERR_PARSE, PS_ERR_MISSING, PS_ERR_TYPE, PS_ERR_RANGE, PS_ERR_BUSY, PS_ERR_HW } ps_err_code_t;
typedef struct { ps_err_code_t code; char field[24]; int32_t limit; char msg[48]; } ps_err_t;

void     ps_err_set(ps_err_t *e, ps_err_code_t code, const char *field, int32_t limit, const char *msg); /* e puede ser NULL */
void     ps_cal_default(ps_cal_t *c);                       /* offset 0, gain 1 */
uint32_t ps_adc_conv_us(uint8_t dr_idx);
int      ps_pick_dr(uint32_t window_us);                    /* DR más lento que cabe; -1 si ninguno */
uint32_t ps_min_sample_step_us(uint32_t settle_us);         /* settle + PS_WRITE_US + conv(DR 860) + PS_ADC_MARGIN_US */
uint32_t ps_r_tia_ohm(uint8_t gain_idx, uint32_t r_ext_ohm);
float    ps_current_na(int16_t adc_raw, uint8_t pga_idx, uint32_t r_tia_ohm, float offset_na, float gain);
float    ps_na_to_fs_frac(float i_na, uint8_t pga_idx, uint32_t r_tia_ohm); /* |i|·R / FSR */
