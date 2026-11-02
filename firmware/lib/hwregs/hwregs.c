/* Los valores de registro del LMP91000 derivan de la biblioteca LMP91000 de Linnes Lab (MIT).
 *
 * Linnes Lab code, firmware, and software is released under the MIT License
 * (http://opensource.org/licenses/MIT).
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Linnes Lab, Purdue University, West Lafayette, IN, USA
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "hwregs.h"

uint8_t lmp_regs_tiacn(uint8_t gain_idx, uint8_t rload_idx)
{
  uint8_t gain = gain_idx < 7 ? (uint8_t)(gain_idx + 1) : 0;    /* 0 = resistencia externa */
  return (uint8_t)((gain << 2) | (rload_idx & 0x03));
}

uint8_t lmp_regs_refcn(int8_t sign, uint8_t bias_idx)
{
  return (uint8_t)(0x80 | 0x20 | (sign > 0 ? 0x10 : 0x00) | (bias_idx & 0x0F)); /* EXT | 50 % | signo | bias */
}

uint16_t ads_config_word(uint8_t pga_idx, uint8_t dr_idx)
{
  /* OS=1, MUX=000 (AIN0-AIN1), PGA, MODE=1 (single-shot), DR, COMP_QUE=00 (RDY tras 1 conversión) */
  return (uint16_t)(0x8000 | ((pga_idx & 0x07) << 9) | 0x0100 | ((dr_idx & 0x07) << 5));
}
