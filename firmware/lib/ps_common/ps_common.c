#include "ps_common.h"
#include <math.h>
#include <string.h>

const uint8_t  PS_BIAS_PCT[PS_NUM_BIAS] = {0, 1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24};
const uint32_t PS_TIA_GAIN_OHM[7] = {2750, 3500, 7000, 14000, 35000, 120000, 350000};
const uint16_t PS_PGA_FSR_MV[6] = {6144, 4096, 2048, 1024, 512, 256};
const uint16_t PS_DR_SPS[8] = {8, 16, 32, 64, 128, 250, 475, 860};

void ps_err_set(ps_err_t *e, ps_err_code_t code, const char *field, int32_t limit, const char *msg)
{
    if (!e) return;
    e->code = code;
    e->limit = limit;
    strncpy(e->field, field ? field : "", sizeof e->field - 1);
    e->field[sizeof e->field - 1] = '\0';
    strncpy(e->msg, msg ? msg : "", sizeof e->msg - 1);
    e->msg[sizeof e->msg - 1] = '\0';
}

void ps_cal_default(ps_cal_t *c)
{
    for (int i = 0; i < 8; i++) { c->offset_na[i] = 0.0f; c->gain[i] = 1.0f; }
}

/* Conversión + 10 % de holgura (reloj del ADS1115 ±10 %), redondeada hacia arriba. */
uint32_t ps_adc_conv_us(uint8_t dr_idx)
{
    uint32_t sps = PS_DR_SPS[dr_idx & 7];
    return (1100000u + sps - 1) / sps;
}

int ps_pick_dr(uint32_t window_us)
{
    for (int i = 0; i < 8; i++)
        if (ps_adc_conv_us((uint8_t)i) <= window_us) return i;
    return -1;
}

uint32_t ps_min_sample_step_us(uint32_t settle_us)
{
    return settle_us + PS_WRITE_US + ps_adc_conv_us(7) + PS_ADC_MARGIN_US;
}

uint32_t ps_r_tia_ohm(uint8_t gain_idx, uint32_t r_ext_ohm)
{
    return gain_idx < PS_GAIN_EXT ? PS_TIA_GAIN_OHM[gain_idx] : r_ext_ohm;
}

float ps_current_na(int16_t adc_raw, uint8_t pga_idx, uint32_t r_tia_ohm, float offset_na, float gain)
{
    float v_uv = (float)adc_raw * (float)PS_PGA_FSR_MV[pga_idx > 5 ? 5 : pga_idx] * 1000.0f / 32768.0f;
    float i_na = v_uv / (float)r_tia_ohm * 1000.0f;
    return (i_na - offset_na) * gain;
}

float ps_na_to_fs_frac(float i_na, uint8_t pga_idx, uint32_t r_tia_ohm)
{
    float v_mv = fabsf(i_na) * (float)r_tia_ohm / 1e6f;   /* nA·Ω = 1e-9 V → /1e6 = mV */
    return v_mv / (float)PS_PGA_FSR_MV[pga_idx > 5 ? 5 : pga_idx];
}
