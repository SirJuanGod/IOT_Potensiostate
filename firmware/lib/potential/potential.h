/* Cálculo de potencial: E de celda -> bias del LMP91000 (signo + %) + código DAC del VREF externo. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "ps_common.h"

typedef struct { int8_t sign; uint8_t bias_idx; uint16_t dac_code; int32_t e_apl_uv; bool snapped; } ps_bias_t;

uint32_t potential_vref_max_uv(uint32_t dac_vref_mv);   /* min(3300000, dac_vref·1000·65535/65536) */
int32_t  potential_e_max_uv(uint32_t dac_vref_mv);      /* 24 % de vref_max */
bool     potential_solve(int32_t e_mv, uint32_t dac_vref_mv, ps_bias_t *out); /* false si |E| > e_max */
