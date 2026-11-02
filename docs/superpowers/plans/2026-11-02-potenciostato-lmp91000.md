# Potenciostato ESP32-C3 + LMP91000 — Plan de implementación

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Firmware nuevo en `firmware/` (ESP-IDF + FreeRTOS) que mide CV/SWV/DPV/NPV/CA con LMP91000 + AD5662 + ADS1115, con ajuste automático en dos pasadas, extracción de características y envío de datos crudos por MQTT.

**Architecture:** La lógica pura (cálculo de potencial, técnicas, validación, análisis, ajuste automático y protocolo) vive en `firmware/lib/*`, sin cabeceras de ESP-IDF, y se prueba en la PC con Unity (`env:native`). El código de hardware y RTOS vive en `firmware/src/*`. `meas_task` se despierta solo por notificaciones (esp_timer + interrupción RDY del ADS1115). `pub_task` publica solo entre pasadas.

**Tech Stack:** C11, ESP-IDF 5.5 vía PlatformIO (`framework = espidf`), FreeRTOS, esp_timer, driver I2C nuevo (`driver/i2c_master.h`), `driver/spi_master.h`, esp-mqtt, NVS, cJSON 1.7.18 (vendorizado, MIT), Unity.

**Spec:** `docs/superpowers/specs/2026-11-02-potenciostato-lmp91000-design.md`

## Global Constraints

- Todo el código nuevo va en `firmware/`. No tocar `data_takes/`.
- `firmware/lib/*` no incluye ninguna cabecera de ESP-IDF ni de FreeRTOS. Debe compilar con `gcc -std=c11` en la PC.
- Ninguna espera activa (`while(now<t)`) ni `vTaskDelay` dentro de una pasada de medición. Los tiempos de los pasos se marcan con esp_timer one-shot a tiempo absoluto (t0 + suma de duraciones).
- I2C: LMP91000 en **0x48**, ADS1115 en **0x49**, 400 kHz. SPI AD5662 modo 1, trama de 24 bits.
- Pines: SPI MOSI=5, SCLK=4, CS=6; I2C SDA=8, SCL=9; ADS RDY=10; LMP MENB=3.
- Potencial: VREF ∈ [1500, min(3300, DAC_VREF·65535/65536)] mV; bias ∈ {0,1,2,4,6,8,10,12,14,16,18,20,22,24} %; |E| ≤ 0,24·VREF_max.
- 0 < |E| < 7,5 mV → 0 mV; 7,5 ≤ |E| < 15 mV → ±15 mV. Ambos casos llevan el flag `snapped`.
- Ganancias del TIA (Ω), índices 0..6: {2750, 3500, 7000, 14000, 35000, 120000, 350000}. Índice 7 = externa (`r_ext_ohm`).
- PGA del ADS1115, índices 0..5: FSR {6144, 4096, 2048, 1024, 512, 256} mV. DR, índices 0..7: {8, 16, 32, 64, 128, 250, 475, 860} SPS. t_conv = ceil(1,1·10⁶/SPS) µs.
- Umbrales por defecto del perfil: saturación 95 %, señal pequeña 20 %, borde 10 %, objetivo 80 %, k = 3.
- Nunca publicar durante una pasada. MQTT con QoS 1, topics `potenciostato/<device_id>/{cmd,status,data,result}`, bloques ≤ 4096 B.
- Los archivos portados o derivados de LinnesLab (LMP91000, MiniStatAnalyst) conservan su aviso MIT. No se copia código de `KickStat.ino`.
- Credenciales WiFi, URI del broker, `device_id` y `DAC_VREF_MV` van en Kconfig, nunca en el código.
- Los comentarios del código se escriben en español, como en el proyecto anterior.

## Review Focus

1. **Barridos que cruzan 0 V y pulsos que caen en la zona |E| < 15 mV.** La medición sigue, el punto se marca `snapped` y `E_apl` refleja el valor real. Lo cubre la Task 2 (`test_snap_zone`) y la Task 4 (`test_validate_accepts_zero_crossing`).
2. **`DAC_VREF_MV` menor que 3300** (referencia de 3,0 V). |E| máximo baja a ~719 mV y la validación rechaza lo que se pase. Lo cubren la Task 2 (`test_vref_3000_limits`) y la Task 4 (`test_profile_check_rejects_beyond_physical`).
3. **JSON de comando malformado, con campos faltantes, tipos equivocados o técnica desconocida.** No hay medición ni cuelgue, y el error nombra el campo. Lo cubre la Task 7 (`test_parse_errors`).
4. **Barridos en sentido inverso** (start > end en SWV/DPV/NPV; CV que empieza bajando). El orden y el número de pasos son correctos. Lo cubre la Task 3 (`test_swv_backward`, `test_cv_starts_down`).
5. **Paso más corto que la conversión más rápida + escrituras + settle** (p. ej. SWV a 500 Hz). Se rechaza con `field = "freq_hz"`. Lo cubre la Task 4 (`test_validate_rejects_too_fast`).

---

## Estructura de archivos

```
firmware/
  platformio.ini            envs: esp32-c3-devkitm-1 (espidf), native (Unity)
  CMakeLists.txt            proyecto ESP-IDF
  sdkconfig.defaults        FREERTOS_HZ=1000, flash 2MB, MQTT buffer
  README.md                 comandos, protocolo, puesta en marcha del hardware
  lib/ps_common/            tipos compartidos, tablas, corriente, DR/PGA
  lib/hwregs/               bits de registros LMP91000 y palabra de config ADS1115 (MIT)
  lib/potential/            E → (signo, bias, código DAC)
  lib/technique/            secuencia de pasos por técnica (por índice, sin RAM)
  lib/profile/              perfil por defecto, profile_check, validación de run
  lib/analysis/             curva, línea base, pico, características CA (MIT)
  lib/autorange/            parámetros de exploración y reglas → definitiva
  lib/protocol/             parseo de comandos/perfil/cal y escritura JSON
  lib/cjson/                cJSON 1.7.18 vendorizado (cJSON.c, cJSON.h, LICENSE)
  lib/model_hook/           stub model_predict() para la etapa 2
  src/CMakeLists.txt, src/Kconfig.projbuild
  src/board.h               pines, direcciones, frecuencias
  src/lmp91000.c/.h  src/ad5662.c/.h  src/ads1115.c/.h
  src/store.c/.h            NVS: perfiles (máx. 4) + calibración
  src/net.c/.h              WiFi STA + esp-mqtt
  src/pub.c/.h              pub_task
  src/engine.c/.h           meas_task
  src/main.c                app_main + despachador de comandos
  test/test_<modulo>/test_main.c
```

---

### Task 1: Esqueleto del proyecto + `ps_common`

**Files:**
- Create: `firmware/platformio.ini`, `firmware/CMakeLists.txt`, `firmware/sdkconfig.defaults`, `firmware/src/CMakeLists.txt`, `firmware/src/Kconfig.projbuild`, `firmware/src/main.c` (versión mínima), `firmware/lib/ps_common/ps_common.h`, `firmware/lib/ps_common/ps_common.c`
- Test: `firmware/test/test_common/test_main.c`

**Interfaces:**
- Produces (`ps_common.h`):
  ```c
  #define PS_NUM_BIAS 14
  extern const uint8_t  PS_BIAS_PCT[PS_NUM_BIAS];
  #define PS_GAIN_EXT 7
  extern const uint32_t PS_TIA_GAIN_OHM[7];
  extern const uint16_t PS_PGA_FSR_MV[6];
  extern const uint16_t PS_DR_SPS[8];
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
  ```

- [ ] **Step 1: Crear el esqueleto PlatformIO**
  - `platformio.ini`:
    - `[env:esp32-c3-devkitm-1]` con `platform = espressif32` (igual que el proyecto anterior), `board = esp32-c3-devkitm-1`, `framework = espidf`, `board_upload.flash_size = 2MB`, `monitor_speed = 115200`.
    - `[env:native]` con `platform = native`, `test_framework = unity`, `build_flags = -std=c11 -Wall -Wextra -Werror`, `lib_ignore = model_hook`.
  - `CMakeLists.txt` estándar de ESP-IDF; `project(potenciostato)`.
  - `sdkconfig.defaults`: `CONFIG_FREERTOS_HZ=1000`, `CONFIG_ESPTOOLPY_FLASHSIZE_2MB=y`, `CONFIG_MQTT_BUFFER_SIZE=4096`.
  - `src/CMakeLists.txt`: `idf_component_register(SRCS "main.c" INCLUDE_DIRS "." REQUIRES esp_driver_i2c esp_driver_spi esp_driver_gpio esp_timer esp_wifi esp_netif esp_event nvs_flash mqtt)`.
  - `Kconfig.projbuild`, menú "Potenciostato": `PS_WIFI_SSID`, `PS_WIFI_PASSWORD`, `PS_MQTT_URI` (default `mqtt://broker.hivemq.com`), `PS_DEVICE_ID` (default `ps01`), `PS_DAC_VREF_MV` (int, default 3300, rango 1500–5000).
  - Instalar PlatformIO si falta: `pip install platformio`.

- [ ] **Step 2: Escribir los tests de `ps_common`** en `test/test_common/test_main.c`
  ```c
  void test_pick_dr(void){ TEST_ASSERT_EQUAL(0, ps_pick_dr(200000)); TEST_ASSERT_EQUAL(4, ps_pick_dr(10000)); TEST_ASSERT_EQUAL(-1, ps_pick_dr(1000)); }
  void test_conv_us(void){ TEST_ASSERT_EQUAL_UINT32(1280, ps_adc_conv_us(7)); TEST_ASSERT_EQUAL_UINT32(137500, ps_adc_conv_us(0)); }
  void test_current(void){ TEST_ASSERT_FLOAT_WITHIN(0.01f, 73142.857f, ps_current_na(16384, 2, 14000, 0, 1));
                           TEST_ASSERT_FLOAT_WITHIN(0.01f, (73142.857f-10)*2, ps_current_na(16384, 2, 14000, 10, 2)); }
  void test_r_tia(void){ TEST_ASSERT_EQUAL_UINT32(350000, ps_r_tia_ohm(6, 0)); TEST_ASSERT_EQUAL_UINT32(2200000, ps_r_tia_ohm(7, 2200000)); }
  void test_fs_frac(void){ TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.5f, ps_na_to_fs_frac(73142.857f, 2, 14000)); }
  ```

- [ ] **Step 3: Verificar que fallan:** `cd firmware && pio test -e native -f test_common` → FAIL (no compila: faltan símbolos).

- [ ] **Step 4: Implementar `ps_common.c`.** La calibración se aplica como `(i − offset)·gain`. `ps_current_na`: `v_uV = raw·FSR_mV·1000/32768`; `i_nA = v_uV/R·1000`.

- [ ] **Step 5: Verificar que pasan:** `pio test -e native -f test_common` → PASS.

- [ ] **Step 6: Verificar la build del dispositivo con una lib/ enlazada.**
  - `src/main.c` mínimo: `app_main` registra con `ESP_LOGI` el valor de `ps_adc_conv_us(7)`.
  - Correr `pio run -e esp32-c3-devkitm-1` → SUCCESS.
  - Si falla porque PlatformIO no incluye `lib/` en la build de ESP-IDF: añadir en cada `lib/<x>/` un `CMakeLists.txt` con `idf_component_register(SRCS <x>.c INCLUDE_DIRS .)` (más `REQUIRES` de las otras libs que use), poner `set(EXTRA_COMPONENT_DIRS lib)` en el `CMakeLists.txt` raíz y repetir. Anotar en `firmware/README.md` cuál de las dos variantes quedó.

- [ ] **Step 7: Commit:** `git add firmware && git commit -m "feat(firmware): esqueleto ESP-IDF/PlatformIO y ps_common"`

---

### Task 2: Cálculo de potencial (`lib/potential`)

**Files:** Create `firmware/lib/potential/potential.h`, `potential.c` · Test `firmware/test/test_potential/test_main.c`

**Interfaces:**
- Consumes: `PS_BIAS_PCT`, `PS_VREF_*` (Task 1).
- Produces:
  ```c
  typedef struct { int8_t sign; uint8_t bias_idx; uint16_t dac_code; int32_t e_apl_uv; bool snapped; } ps_bias_t;
  uint32_t potential_vref_max_uv(uint32_t dac_vref_mv);        /* min(3300000, dac_vref·1000·65535/65536) */
  int32_t  potential_e_max_uv(uint32_t dac_vref_mv);           /* 24 % de vref_max */
  bool     potential_solve(int32_t e_mv, uint32_t dac_vref_mv, ps_bias_t *out); /* false si |E| > e_max */
  ```

- [ ] **Step 1: Tests**
  ```c
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
  ```
- [ ] **Step 2:** `pio test -e native -f test_potential` → FAIL.
- [ ] **Step 3: Implementar.** Toda la aritmética es entera en µV con `uint64_t`, sin float (el C3 no tiene FPU).
  - Para cada `idx` de 1 a 13: `vref = |E|·100/pct`. Si cae fuera de `[1500000, vref_max]`, se descarta.
  - `code = round(vref·65536/dac_vref_uv)`. Si `code > 65535` se baja a 65535.
  - Si el VREF aplicado cae fuera del rango, se ajusta `code` en ±1 para volver a entrar.
  - `e_apl = vref_apl·pct/100`. Se elige el menor error y, en empate, el mayor `code`.
  - Para E = 0: bias 0 y `code` de `PS_VREF_ZERO_MV`.
- [ ] **Step 4:** `pio test -e native -f test_potential` → PASS.
- [ ] **Step 5:** `git commit -m "feat(firmware): cálculo de potencial bias+DAC"`

---

### Task 3: Secuencia de pasos (`lib/technique`)

**Files:** Create `firmware/lib/technique/technique.h`, `technique.c` · Test `firmware/test/test_technique/test_main.c`

**Interfaces:**
- Consumes: `ps_params_t`, `ps_step_t`, tags y branches (Task 1).
- Produces:
  ```c
  uint32_t tech_num_steps(const ps_params_t *p);
  uint32_t tech_num_samples(const ps_params_t *p);
  bool     tech_step_at(const ps_params_t *p, uint32_t k, ps_step_t *out);   /* false si k >= num_steps */
  bool     tech_has_window(ps_tec_t tec);                                    /* true en SWV, DPV y NPV */
  void     tech_get_window(const ps_params_t *p, int16_t *lo, int16_t *hi, int16_t *step); /* step NPV = amp */
  void     tech_set_window(ps_params_t *p, int16_t lo, int16_t hi, int16_t step);         /* conserva el sentido start→end */
  void     tech_e_extremes(const ps_params_t *p, int16_t *e_min, int16_t *e_max);        /* incluye ±amp y vértices */
  ```
  Semántica (spec §8): `tech_step_at` es sin estado y calcula el paso k con aritmética, sin generar la lista en RAM. El sentido lo da el signo de (end − start).
  - **CV:** dur = step·10⁶/rate µs. Cada ciclo recorre start→vertex1, vertex1∓step→vertex2 y vertex2±step→end, sin repetir vértices. Si end == start, los ciclos siguientes saltan su primer punto. `branch` = UP o DOWN según el sentido del segmento; `tag` = STEP.
  - **SWV:** por cada x: (x+amp, FWD) y luego (x−amp, REV), cada uno de 10⁶/(2f) µs.
  - **DPV:** por cada x: (x, BASE, period−width) y luego (x+amp, PULSE, width).
  - **NPV:** primero (start, QUIET, quiet, `sample=false`). Luego, para x = start, start±amp, … hasta end: (x, PULSE, width) seguido de (start, BASE, period−width).
  - **CA:** tres segmentos {pre_step/quiet, v1/t1, v2/t2}, cada uno con `samples` pasos de t/samples. `branch` = segmento; `x_mv` = potencial del segmento.
  - En SWV, DPV y NPV, `x_mv` = x (potencial base) y `e_mv` = potencial aplicado. En CV y CA, `x_mv = e_mv`.

- [ ] **Step 1: Tests**
  ```c
  static ps_params_t swv(int s,int e){ ps_params_t p={.tec=PS_TEC_SWV}; p.p.swv=(ps_swv_t){s,e,50,1,25.0f}; return p; }
  void test_swv_forward(void){ ps_params_t p=swv(-400,500); ps_step_t st;
    TEST_ASSERT_EQUAL_UINT32(1802, tech_num_steps(&p)); TEST_ASSERT_EQUAL_UINT32(1802, tech_num_samples(&p));
    tech_step_at(&p,0,&st); TEST_ASSERT_EQUAL_INT16(-350,st.e_mv); TEST_ASSERT_EQUAL_INT16(-400,st.x_mv); TEST_ASSERT_EQUAL_UINT32(20000,st.dur_us); TEST_ASSERT_EQUAL(PS_TAG_FWD,st.tag);
    tech_step_at(&p,1,&st); TEST_ASSERT_EQUAL_INT16(-450,st.e_mv); TEST_ASSERT_EQUAL(PS_TAG_REV,st.tag);
    tech_step_at(&p,1801,&st); TEST_ASSERT_EQUAL_INT16(500,st.x_mv); TEST_ASSERT_EQUAL_INT16(450,st.e_mv);
    TEST_ASSERT_FALSE(tech_step_at(&p,1802,&st)); }
  void test_swv_backward(void){ ps_params_t p=swv(500,-400); ps_step_t st; tech_step_at(&p,0,&st);
    TEST_ASSERT_EQUAL_INT16(500,st.x_mv); TEST_ASSERT_EQUAL_INT16(550,st.e_mv); tech_step_at(&p,2,&st); TEST_ASSERT_EQUAL_INT16(499,st.x_mv); }
  void test_dpv(void){ /* 0→100, amp 50, step 10, period 200, width 50 */ 22 pasos; paso0 {e 0,x 0,dur 150000,BASE}; paso1 {e 50,x 0,dur 50000,PULSE} }
  void test_npv(void){ /* -200→0, amp 50, width 50, period 200, quiet 500 */ 11 pasos, 10 muestras;
    paso0 {e -200,dur 500000,QUIET,sample false}; paso1 {e -200,PULSE,dur 50000}; paso2 {e -200,BASE,dur 150000}; paso3 {e -150,PULSE}; paso10 {e -200,BASE} }
  void test_cv_two_cycles(void){ /* cycles 2, start 0, end 0, v1 450, v2 -300, step 5, rate 50 */ 601 pasos, dur 100000;
    paso90 {e 450,UP,cycle 0}; paso91 {e 445,DOWN}; paso300 {e 0,UP,cycle 0}; paso301 {e 5,UP,cycle 1} }
  void test_cv_starts_down(void){ /* start 0, v1 -300, v2 450, end 0, step 5, 1 ciclo */ paso1 {e -5,DOWN}; paso60 {e -300}; paso61 {e -295,UP} }
  void test_ca(void){ /* pre 0, quiet 1000, v1 -200, t1 2000, v2 200, t2 2000, samples 10 */ 30 pasos;
    paso0 {e 0,dur 100000,branch 0,sample true}; paso10 {e -200,dur 200000,branch 1} }
  void test_extremes(void){ swv(-400,500) → e_min -450, e_max 550 }
  void test_window_roundtrip(void){ tech_set_window(swv(-400,500), -100, 300, 2) → start -100, end 300, step 2; con swv(500,-400) → start 300, end -100 }
  ```
  (El implementador escribe las aserciones marcadas con llaves en el mismo estilo de los dos primeros tests.)
- [ ] **Step 2:** `pio test -e native -f test_technique` → FAIL.
- [ ] **Step 3:** Implementar `technique.c` según la semántica de arriba.
- [ ] **Step 4:** `pio test -e native -f test_technique` → PASS.
- [ ] **Step 5:** `git commit -m "feat(firmware): generador de pasos CV/SWV/DPV/NPV/CA"`

---

### Task 4: Perfil y validación (`lib/profile`)

**Files:** Create `firmware/lib/profile/profile.h`, `profile.c` · Test `firmware/test/test_profile/test_main.c`

**Interfaces:**
- Consumes: Task 1, `potential_e_max_uv` (Task 2), `tech_*` (Task 3).
- Produces:
  ```c
  typedef struct {
    char name[16];
    int16_t e_min_mv, e_max_mv; uint16_t step_min_mv, step_max_mv, amp_max_mv;
    float freq_min_hz, freq_max_hz; uint16_t rate_min_mv_s, rate_max_mv_s;
    uint8_t gain_mask;            /* bit i = índice de ganancia i permitido (0..7) */
    uint8_t default_gain_idx, rload_idx; uint32_t r_ext_ohm, settle_us; uint16_t max_points;
    int8_t redox; ps_win_t peak_win, base_win1, base_win2;
    uint8_t sat_pct, small_pct, edge_pct, target_pct, fwhm_k;
    ps_params_t defaults;
  } ps_profile_t;
  void profile_default(ps_profile_t *p);
  bool profile_check(const ps_profile_t *p, uint32_t dac_vref_mv, ps_err_t *err);
  bool profile_validate_run(const ps_profile_t *p, const ps_params_t *run, uint8_t gain_idx, uint32_t dac_vref_mv, ps_err_t *err);
  ```
  - **`profile_default`:** name `"default"`, E ±600, step 1..20, amp_max 100, freq 1..100 Hz, rate 5..500, gain_mask 0x7F, default_gain 3, rload 0, r_ext 0, settle 500 µs, max_points 4000, redox +1, peak_win [−600,600], base [−600,−500] y [500,600], umbrales 95/20/10/80/3, defaults = SWV {−400, 500, 25, 2, 25 Hz}.
  - **`profile_check`:** exige e_min < e_max y que los dos estén dentro de ±e_max físico (Task 2). Las ventanas deben estar ordenadas y dentro de [e_min, e_max]. Además step_min ≤ step_max, freq_min ≤ freq_max, rate_min ≤ rate_max, `max_points` ≤ `PS_MAX_SAMPLES`, `redox` = ±1 y `default_gain_idx` permitido. `field` = nombre del campo del struct.
  - **`profile_validate_run`:** el error nombra el campo del comando JSON (`start`, `end`, `vertex1`, `vertex2`, `pre_step`, `v1`, `v2`, `step`, `amp`, `freq_hz`, `rate_mV_s`, `gain`, `points`). Comprueba:
    - extremos (`tech_e_extremes`) dentro del perfil y de la física;
    - step y amp dentro de límites;
    - freq y rate dentro de rango;
    - ganancia permitida, y con índice 7 exige `r_ext_ohm > 0`;
    - `tech_num_samples` ≤ `max_points`;
    - cada paso con `sample` dura ≥ `ps_min_sample_step_us(settle_us)` (el error va a `freq_hz` en SWV, `rate_mV_s` en CV, `width_ms` en DPV/NPV y `samples` en CA).
  - Los potenciales en la zona de redondeo (|E| < 15 mV) **no** son error.

- [ ] **Step 1: Tests:** `test_default_is_valid` (profile_check OK con 3300), `test_validate_accepts_zero_crossing` (SWV −400..500 amp 50 step 1 → true), `test_validate_rejects_amp` (amp 150 → false, field `"amp"`, limit 100), `test_validate_rejects_too_fast` (SWV freq_hz 250 con freq_max 1000 en un perfil modificado → false, field `"freq_hz"`), `test_validate_rejects_points` (max_points 1000 con SWV step 1 → field `"points"`), `test_validate_rejects_ext_gain_without_r` (gain 7, r_ext 0 → field `"gain"`), `test_validate_rejects_out_of_profile` (CV vertex1 650 → field `"vertex1"`, limit 600), `test_profile_check_rejects_beyond_physical` (e_max 750 con dac_vref 3000 → field `"e_max_mv"`), `test_profile_check_rejects_window_outside` (base_win2 [500,700] → field `"base_win2"`).
  - En `too_fast`, a 250 Hz cada paso dura 2000 µs, menos que el mínimo de 500 + 300 + 1280 + 200 = 2280 µs. En `zero_crossing` se usa freq_hz 25.
- [ ] **Step 2:** FAIL · **Step 3:** implementar · **Step 4:** `pio test -e native -f test_profile` → PASS
- [ ] **Step 5:** `git commit -m "feat(firmware): perfil de compuesto y validación de límites"`

---

### Task 5: Análisis (`lib/analysis`)

**Files:** Create `firmware/lib/analysis/analysis.h`, `analysis.c` (cabecera con aviso MIT de LinnesLab: idea de MiniStatAnalyst reimplementada) · Test `firmware/test/test_analysis/test_main.c`

**Interfaces:**
- Consumes: Task 1, `ps_profile_t` (Task 4).
- Produces:
  ```c
  typedef struct { float base_m, base_b, ip_na, area_na_mv, fwhm_mv; int16_t ep_mv; bool at_edge; } ps_peak_feat_t;
  typedef struct { float iss_na[3], q_nc[3]; } ps_ca_feat_t;
  typedef struct { bool has_peak; ps_peak_feat_t pk; ps_ca_feat_t ca; } ps_features_t;
  size_t analysis_build_curve(const ps_sample_t *s, const float *i_na, size_t n, ps_tec_t tec, int8_t redox, int16_t *x_mv, float *y_na); /* devuelve número de puntos */
  bool   analysis_baseline(const int16_t *x, const float *y, size_t n, ps_win_t w1, ps_win_t w2, float *m, float *b);
  bool   analysis_peak(const int16_t *x, const float *y, size_t n, float m, float b, ps_win_t search, int8_t redox, uint8_t edge_pct, ps_peak_feat_t *out);
  void   analysis_ca(const ps_sample_t *s, const float *i_na, size_t n, ps_ca_feat_t *out);
  bool   analysis_features(const ps_sample_t *s, const float *i_na, size_t n, ps_tec_t tec, const ps_profile_t *prof, int16_t *x_buf, float *y_buf, ps_features_t *out);
  ```
  - **`build_curve`:**
    - SWV: pares consecutivos FWD/REV con el mismo `x_mv` → y = FWD − REV.
    - DPV: BASE seguido de PULSE → PULSE − BASE.
    - NPV: PULSE seguido de BASE → PULSE − BASE. Se ignora QUIET.
    - CV: solo el último ciclo, branch UP si redox = +1 y DOWN si −1, con y = i.
    - CA: devuelve 0.
  - **`baseline`:** mínimos cuadrados sobre los puntos que caen en w1 ∪ w2. Devuelve false si son menos de 2.
  - **`peak`:**
    - corregido(x) = redox·(y − (m·x + b)); el máximo se busca en `search`. Devuelve false si el máximo ≤ 0.
    - `ip_na` = corregido·redox (con signo).
    - `fwhm_mv`: cruces a media altura con interpolación lineal. Si falta un cruce, se usa el borde del rango.
    - `area_na_mv`: trapecio del corregido·redox entre los cruces.
    - `at_edge`: el índice del pico está dentro del `edge_pct` % del rango medido [x mínimo, x máximo].
  - **`analysis_ca`:** por segmento (branch 0..2), `iss` = media del último 20 % de muestras y `q_nC` = trapecio de i·dt con t en µs (nA·µs/10⁶ = nC·10⁻³ → convertir a nC).
  - **`features`:** orquesta las funciones anteriores según la técnica. `has_peak` = false si falla la línea base o el pico.

- [ ] **Step 1: Tests**
  - `test_gaussian_peak`: x = −500..500 cada 5; y = 0,01x + 2 + 100·exp(−((x−100)/40)²); ventanas [−500,−300] y [300,500]; search [−200,300]; redox +1. Esperado: ip 100±1, ep 100, fwhm 66,6±5, m 0,01±0,001.
  - `test_negative_peak`: la misma curva con el pico negado y redox −1. Esperado: ip −100±1.
  - `test_baseline_too_few_points`: ventanas sin puntos → false.
  - `test_peak_at_edge`: el mismo gaussiano con x medido −200..110 → `at_edge` true.
  - `test_build_curve_swv`: 3 pares FWD = 10, REV = 4 → 3 puntos con y = 6 y x correcto.
  - `test_build_curve_cv_last_cycle`: 2 ciclos sintéticos, redox +1 → solo puntos UP del ciclo 1.
  - `test_ca_features`: 3 segmentos de 10 muestras a 5, 7 y 9 nA, separadas 100000 µs. Esperado: iss = {5, 7, 9} y q_nC del segmento 0 = 5 nA·0,9 s = 4,5 nC (trapecio entre la primera y la última muestra).
- [ ] **Step 2:** FAIL · **Step 3:** implementar (float está bien aquí) · **Step 4:** `pio test -e native -f test_analysis` → PASS
- [ ] **Step 5:** `git commit -m "feat(firmware): línea base, pico y características"`

---

### Task 6: Ajuste automático (`lib/autorange`)

**Files:** Create `firmware/lib/autorange/autorange.h`, `autorange.c` · Test `firmware/test/test_autorange/test_main.c`

**Interfaces:**
- Consumes: Tasks 1, 3, 4 y 5.
- Produces:
  ```c
  typedef struct { uint16_t max_abs_raw; bool has_peak; float peak_fs_frac; int16_t ep_mv; float fwhm_mv; bool at_edge; } autorange_input_t;
  typedef struct { uint8_t gain_idx, pga_idx; } ps_gain_knobs_t;
  typedef enum { PS_RULE_SATURATION, PS_RULE_SMALL, PS_RULE_EDGE, PS_RULE_ZOOM, PS_RULE_STEP_BUDGET } ps_rule_t;
  typedef struct { ps_rule_t rule; int32_t b0, b1, a0, a1; } ps_adj_t;   /* antes/después; ganancia: b0=gain, b1=pga */
  void   autorange_explore_params(const ps_params_t *req, const ps_profile_t *prof, ps_params_t *out);
  size_t autorange_decide(const autorange_input_t *in, const ps_profile_t *prof, const ps_params_t *req,
                          const ps_gain_knobs_t *cur, ps_params_t *final_p, ps_gain_knobs_t *final_k,
                          ps_adj_t *adj, size_t max_adj, bool *peak_at_edge);
  ```
  - **Exploración:** si `tech_has_window(tec)`, la ventana es la del perfil, ajustada por amp: SWV [e_min+amp, e_max−amp]; DPV [e_min, e_max−amp]; NPV conserva `start` y lleva `end` al límite en su sentido. El paso es `min(4·step, step_max)` (en NPV, `min(4·amp, step_max)`). Se conserva el sentido start→end. CV y CA se copian sin cambios.
  - **Reglas, en este orden:**
    1. **Saturación:** `max_abs_raw/32767·100 > sat_pct` → pga−1. Si ya está en 0, se baja a la siguiente ganancia permitida.
    2. **Señal pequeña:** solo si `has_peak` y `peak_fs_frac·100 < small_pct`. Mientras la proyección `max_abs_frac·FSR_actual/FSR_nuevo·R_nuevo/R_actual` ≤ `target_pct`, se sube primero la PGA y luego la ganancia (permitida).
    3. **Solo en técnicas con ventana:** si no hay pico → ventana y paso pedidos. Si `at_edge` → ventana explorada, paso pedido y `*peak_at_edge` = true. Si no → ventana = [ep − k·fwhm, ep + k·fwhm] ∪ base_win1 ∪ base_win2, recortada al perfil (con ajuste por amp como en la exploración), con paso pedido.
    4. **Presupuesto de pasos:** si `tech_num_samples(final) > max_points`, se sube el paso al mínimo que quepa.
  - Cada cambio queda registrado en `adj`. La función devuelve cuántos se registraron.

- [ ] **Step 1: Tests**
  - Todos con `profile_default`, `cur = {gain 3, pga 2}` y la técnica indicada.
  - `test_explore_swv`: pedido SWV −400..500, amp 50, step 2 → explora −550..550 con step 8.
  - `test_saturation_pga`: max_abs 32000 → pga 1, regla SATURATION.
  - `test_saturation_gain`: max_abs 32000 con pga 0 → gain 2, pga 0.
  - `test_small_signal_pga_then_stop`: peak 0,05, max_abs_frac 0,05 → pga 5, gain 3.
  - `test_small_signal_limited_by_total`: peak 0,05, max_abs_frac 0,30 → pga 3.
  - `test_zoom_with_near_baselines`: perfil con base [−100,−50] y [250,300], ep 100, fwhm 66 → ventana final [−100, 300] (porque 100 ± 198 = [−98, 298]), step 2.
  - `test_edge_keeps_window`: at_edge → ventana = la explorada y `peak_at_edge` true.
  - `test_no_peak_keeps_requested`: has_peak false + saturación → ventana pedida y pga 1.
  - `test_ca_gain_only`: CA con saturación → parámetros iguales y pga 1.
  - `test_step_budget`: SWV, perfil con max_points 200 y ventana de 1000 mV a step 1 → step final 11 (2·(⌊1000/11⌋+1) = 182 ≤ 200; con 10 serían 202) y regla STEP_BUDGET.
- [ ] **Step 2:** FAIL · **Step 3:** implementar · **Step 4:** `pio test -e native -f test_autorange` → PASS
- [ ] **Step 5:** `git commit -m "feat(firmware): reglas de ajuste automático en dos pasadas"`

---

### Task 7: Protocolo JSON (`lib/protocol` + `lib/cjson`)

**Files:** Create `firmware/lib/cjson/{cJSON.c,cJSON.h,LICENSE}` (cJSON v1.7.18 copiado del release oficial de DaveGamble/cJSON), `firmware/lib/protocol/protocol.h`, `protocol.c` · Test `firmware/test/test_protocol/test_main.c`

**Interfaces:**
- Consumes: Tasks 1, 4, 5 y 6.
- Produces:
  ```c
  typedef enum { PS_CMD_RUN, PS_CMD_STOP, PS_CMD_SET_PROFILE, PS_CMD_GET_PROFILE, PS_CMD_SELECT_PROFILE, PS_CMD_SET_CAL, PS_CMD_GET_CAL } ps_cmd_kind_t;
  typedef enum { PS_PASS_EXPLORE, PS_PASS_FINAL } ps_pass_t;
  typedef struct { ps_cmd_kind_t kind; char id[32]; bool auto_adj, gain_auto; uint8_t gain_idx; ps_params_t params;
                   ps_profile_t profile; char name[16]; ps_cal_t cal; } ps_cmd_t;
  typedef struct { const char *id; ps_pass_t pass; const ps_params_t *params; const char *profile_name;
                   uint8_t gain_idx, rload_idx, pga_idx; uint32_t dac_vref_mv, n_samples; uint64_t t0_us; const char *fw_version; } ps_run_meta_t;
  #define PS_RUN_NO_PEAK 0x01
  #define PS_RUN_PEAK_AT_EDGE 0x02
  #define PS_RUN_ABORTED 0x04
  #define PS_RUN_ERROR 0x08
  bool proto_parse_cmd(const char *json, size_t len, ps_cmd_t *out, ps_err_t *err);
  int  proto_write_status(char *buf, size_t cap, const char *state, const ps_err_t *err /*NULL si no hay*/);
  int  proto_write_header(char *buf, size_t cap, const ps_run_meta_t *m);
  int  proto_write_rows(char *buf, size_t cap, const char *id, ps_pass_t pass, uint32_t seq,
                        const ps_sample_t *s, const float *i_na, size_t n, size_t *consumed, bool *last);
  int  proto_write_result(char *buf, size_t cap, const char *id, ps_tec_t tec, const ps_features_t *f,
                          const ps_adj_t *adj, size_t n_adj, uint32_t run_flags);
  int  proto_write_profile(char *buf, size_t cap, const ps_profile_t *p);
  int  proto_write_cal(char *buf, size_t cap, const ps_cal_t *c);
  ```
  - **Claves** (exactas del spec §8/§11):
    - `cmd` ∈ {`run`, `stop`, `set_profile`, `get_profile`, `select_profile`, `set_calibration`, `get_calibration`}; `tec` ∈ {`cv`, `swv`, `dpv`, `npv`, `ca`}.
    - Parámetros: cv `cycles, start, end, vertex1, vertex2, step, rate_mV_s`; swv `start, end, amp, step, freq_hz`; dpv `start, end, amp, step, period_ms, width_ms`; npv `start, end, amp, width_ms, period_ms, quiet_ms`; ca `pre_step, quiet_ms, v1, t1_ms, v2, t2_ms, samples`.
    - `gain`: `"auto"` o entero 0..7 (si falta, `"auto"`). `auto`: bool (si falta, false). `id`: string de 1 a 31 caracteres, obligatorio en `run`.
  - El perfil y la calibración en JSON usan los nombres de campo del struct. Las ventanas van como `[lo, hi]`; `defaults` como `{"tec":..., "params":{...}}`; la calibración como `{"offset_na":[8], "gain":[8]}`.
  - Las filas siguen el orden de columnas del spec §11. La cabecera incluye `cols`. `proto_write_rows` escribe tantas filas como quepan en `cap` y fija `*last`.
  - Los writers devuelven la longitud escrita, o −1 si ni siquiera cabe una fila o el mensaje.

- [ ] **Step 1: Tests**
  - `test_parse_run_swv`: el comando de ejemplo del spec → kind RUN, id `"muestra_07"`, auto true, gain_auto true y los parámetros exactos.
  - `test_parse_stop`.
  - `test_parse_errors`, con estos casos:
    - `"{bad"` → PARSE;
    - `run` sin `id` → MISSING `"id"`;
    - `"tec":"eis"` → RANGE `"tec"`;
    - `"start":"x"` → TYPE `"start"`;
    - `"gain":9` → RANGE `"gain"`;
    - `cmd` desconocido → RANGE `"cmd"`.
  - `test_profile_roundtrip`: `proto_write_profile(profile_default)` envuelto en `{"cmd":"set_profile","profile":...}` y parseado de nuevo → struct igual (`memcmp` por campos).
  - `test_cal_roundtrip`: lo mismo con la calibración.
  - `test_rows_chunking`: 500 muestras con cap 4096 → varios bloques; la suma de `consumed` = 500, el último lleva `last`, y cada bloque es JSON válido (`cJSON_Parse` != NULL).
  - `test_result_flags`: con `PS_RUN_NO_PEAK|PS_RUN_ABORTED` → `"flags":["no_peak","aborted"]`.
- [ ] **Step 2:** FAIL · **Step 3:** implementar. Se parsea con cJSON y las filas se escriben con `snprintf` (`%.3f` para `i_nA`). · **Step 4:** `pio test -e native -f test_protocol` → PASS
- [ ] **Step 5:** `git commit -m "feat(firmware): protocolo MQTT JSON"`

---

### Task 8: Drivers LMP91000, AD5662 y ADS1115

**Files:** Create `firmware/lib/hwregs/hwregs.h`, `hwregs.c` (aviso MIT de LinnesLab), `firmware/src/board.h`, `firmware/src/lmp91000.{c,h}`, `firmware/src/ad5662.{c,h}`, `firmware/src/ads1115.{c,h}`; Modify `firmware/src/CMakeLists.txt` (SRCS) · Test `firmware/test/test_hwregs/test_main.c`

**Interfaces:**
- Produces (pure, `hwregs.h`):
  ```c
  uint8_t  lmp_regs_tiacn(uint8_t gain_idx, uint8_t rload_idx);   /* gain 0..6 → código idx+1; 7 → 0 (EXT) */
  uint8_t  lmp_regs_refcn(int8_t sign, uint8_t bias_idx);         /* EXT ref | INT_Z 50 % | signo | bias */
  #define  LMP_MODECN_3LEAD 0x03
  uint16_t ads_config_word(uint8_t pga_idx, uint8_t dr_idx);      /* OS=1, MUX=AIN0−AIN1, single, COMP_QUE=00 */
  ```
- Produces (device):
  ```c
  esp_err_t lmp_init(i2c_master_bus_handle_t bus);               /* MENB bajo; espera STATUS ready con vTaskDelay (arranque, fuera de pasada) */
  esp_err_t lmp_configure(uint8_t gain_idx, uint8_t rload_idx);  /* unlock, TIACN, REFCN(+,0), MODECN 3-lead; queda desbloqueado */
  esp_err_t lmp_set_bias(int8_t sign, uint8_t bias_idx);         /* escribe REFCN solo si cambió */
  esp_err_t lmp_lock(void);
  esp_err_t ad5662_init(void);
  esp_err_t ad5662_write(uint16_t code);                         /* spi_device_polling_transmit, 10 MHz */
  esp_err_t ads_init(i2c_master_bus_handle_t bus, TaskHandle_t notify, uint32_t notify_bit); /* Hi_thresh=0x8000, Lo=0x0000 (modo RDY); ISR en flanco de bajada de GPIO10 → xTaskNotifyFromISR(eSetBits) */
  esp_err_t ads_start(uint8_t pga_idx, uint8_t dr_idx);
  esp_err_t ads_read(int16_t *raw);
  ```
  `board.h`: los pines y direcciones de Global Constraints, `PS_I2C_HZ 400000` y `PS_SPI_HZ 10000000`.

- [ ] **Step 1: Tests** (`test_hwregs`):
  - `lmp_regs_tiacn(3,0) == 0x10`; `lmp_regs_tiacn(7,3) == 0x03`; `lmp_regs_tiacn(6,0) == 0x1C`.
  - `lmp_regs_refcn(1,13) == 0xBD`; `lmp_regs_refcn(-1,0) == 0xA0`.
  - `ads_config_word(2,7) == 0x85E0`; `ads_config_word(0,0) == 0x8100`.
- [ ] **Step 2:** FAIL · **Step 3:** implementar `hwregs.c` · **Step 4:** `pio test -e native -f test_hwregs` → PASS
- [ ] **Step 5: Implementar los drivers de `src/`.**
  - Bus I2C con `i2c_new_master_bus` (puerto 0, pull-ups internos activados solo como respaldo). Dos dispositivos con `i2c_master_bus_add_device`.
  - LMP91000: escritura de registro = `i2c_master_transmit({reg, val})`; lectura = `i2c_master_transmit_receive`.
  - ADS1115: config = `{0x01, hi, lo}`; lectura = registro 0x00, big-endian.
  - Errores: cualquier fallo de I2C o SPI devuelve `esp_err_t`; nada de `ESP_ERROR_CHECK` dentro de los drivers.
- [ ] **Step 6:** `pio run -e esp32-c3-devkitm-1` → SUCCESS.
- [ ] **Step 7:** `git commit -m "feat(firmware): drivers LMP91000, AD5662 y ADS1115"`

---

### Task 9: NVS, red y publicación (`store`, `net`, `pub`)

**Files:** Create `firmware/src/store.{c,h}`, `firmware/src/net.{c,h}`, `firmware/src/pub.{c,h}`; Modify `firmware/src/CMakeLists.txt`

**Interfaces:**
- Consumes: `profile_*` (Task 4) y `proto_*` (Task 7).
- Produces:
  ```c
  esp_err_t store_init(void);                                  /* nvs_flash_init; si NO_FREE_PAGES o NEW_VERSION → erase + init */
  esp_err_t store_get_active_profile(ps_profile_t *out);       /* si no hay ninguno, profile_default */
  esp_err_t store_put_profile(const ps_profile_t *p);          /* máximo 4; reemplaza por nombre; ESP_ERR_NO_MEM si ya hay 4 y es nuevo */
  esp_err_t store_select_profile(const char *name);            /* ESP_ERR_NOT_FOUND */
  esp_err_t store_get_cal(ps_cal_t *out);                      /* si no hay, ps_cal_default */
  esp_err_t store_put_cal(const ps_cal_t *c);
  typedef void (*net_cmd_cb_t)(const char *data, int len);
  esp_err_t net_start(net_cmd_cb_t cb);                        /* no bloquea; MQTT arranca con IP_EVENT_STA_GOT_IP */
  esp_err_t net_publish(const char *sub, const char *data, int len, bool retain); /* ESP_ERR_INVALID_STATE si no hay conexión */
  bool      net_connected(void);
  typedef struct { const ps_run_meta_t *meta; const ps_sample_t *s; const float *i_na; size_t n;
                   const ps_features_t *f; const ps_adj_t *adj; size_t n_adj; uint32_t run_flags; bool send_result; } ps_pass_ctx_t;
  esp_err_t pub_start(void);                                   /* crea pub_task (prioridad 3, stack 6144) */
  void      pub_publish_pass(const ps_pass_ctx_t *ctx, TaskHandle_t done_task); /* encola; al terminar, xTaskNotifyGiveIndexed(done_task, 1) */
  void      pub_status(const char *state, const ps_err_t *err);
  ```
  - **`net`:**
    - WiFi STA con las credenciales de Kconfig. Reconecta indefinidamente con backoff de 1, 2, 4, … hasta 30 s mediante esp_timer (sin bucles bloqueantes).
    - esp-mqtt con `buffer.size = 4096`, LWT `status` = `{"state":"offline"}` con retain, y suscripción a `cmd` al conectar.
    - Un mensaje fragmentado (`total_data_len > data_len`) se descarta y se publica un error PARSE.
  - **`pub_task`:**
    1. publica la cabecera y luego los bloques (`proto_write_rows` hasta `last`), con un buffer estático de 4096 B;
    2. publica el resultado si `send_result`;
    3. avisa a `done_task`.
    - Si no hay conexión, reintenta cada bloque con `vTaskDelay(500)`, que está permitido porque no es ruta de medición. Tras 120 s se rinde, registra el error en el log y avisa igualmente (los datos se pierden; se reporta `status` error `"publish_timeout"`).
- [ ] **Step 1: Implementar los tres módulos.**
- [ ] **Step 2:** `pio run -e esp32-c3-devkitm-1` → SUCCESS.
- [ ] **Step 3:** `git commit -m "feat(firmware): NVS, WiFi/MQTT y tarea de publicación"`

---

### Task 10: Motor de medición, `main` y documentación

**Files:** Create `firmware/src/engine.{c,h}`, `firmware/lib/model_hook/model_hook.{c,h}`, `firmware/README.md`; Modify `firmware/src/main.c`, `firmware/src/CMakeLists.txt`, `CLAUDE.md`

**Interfaces:**
- Consumes: todo lo anterior.
- Produces:
  ```c
  esp_err_t engine_start(void);                 /* crea meas_task (prioridad 10, stack 6144) y los dos esp_timer */
  esp_err_t engine_submit(const ps_cmd_t *run); /* ESP_ERR_INVALID_STATE si está ocupado */
  void      engine_request_stop(void);          /* xTaskNotify(meas_task, EVT_STOP, eSetBits) */
  bool      engine_busy(void);
  bool      model_predict(const ps_features_t *f, const ps_params_t *p, float *conc); /* stub: devuelve false */
  ```
  - **Bits de notificación (índice 0):** `EVT_RUN`, `EVT_STEP_END`, `EVT_ADC_TRIGGER`, `EVT_ADC_RDY`, `EVT_STOP`. El índice 1 queda para "publicación terminada".
  - **Flujo de un run en `meas_task`:**
    1. `profile_validate_run`. Si falla → `pub_status("error", &err)` y fin.
    2. Si `auto`: exploración con `autorange_explore_params` → ejecutar pasada → análisis → publicar (y esperar índice 1) → `autorange_decide` → `profile_validate_run(final)`. Si este último falla, usar los parámetros pedidos y añadir `PS_RUN_ERROR` al log de ajustes.
    3. Pasada definitiva → análisis → publicar con `send_result = true`.
    4. Estado seguro: `lmp_set_bias(+1,0)` + `ad5662_write(code de PS_VREF_ZERO_MV)` + `lmp_lock()` → `pub_status("idle")`.
  - **Ejecutar una pasada:**
    - `lmp_configure(gain, rload)` y luego `vTaskDelay(pdMS_TO_TICKS(max(1, settle_us/1000)))`; esto es antes del primer paso, fuera de la temporización de pasos.
    - Se fija `t0 = esp_timer_get_time()`. Para cada paso k (`tech_step_at`):
      1. `potential_solve` → `lmp_set_bias` + `ad5662_write`;
      2. `t_end = t0 + Σdur`;
      3. si `sample`: `dr = ps_pick_dr(dur − settle_us − PS_WRITE_US)` y se arma `adc_timer` en `t_end − ps_adc_conv_us(dr) − PS_ADC_MARGIN_US`;
      4. se arma `step_timer` en `t_end`;
      5. se espera con `xTaskNotifyWaitIndexed(0, …, portMAX_DELAY)`: con ADC_TRIGGER → `ads_start(pga, dr)`; con ADC_RDY → `ads_read` y se guarda `ps_sample_t` con `t_us = esp_timer_get_time()` y `flags |= snapped`; con STEP_END → paso siguiente, pero si había muestra pendiente se espera RDY con timeout de 2·conv;
      6. con STOP → abortar: estado seguro y publicar lo medido con `PS_RUN_ABORTED` y `send_result = true` (spec §6).
    - Si `ads_read` o el timeout falla → `PS_RUN_ERROR`, estado seguro y `status` error HW con el campo `"adc"`.
  - **Memoria:** `static ps_sample_t s_samples[PS_MAX_SAMPLES]`, `static float s_i_na[PS_MAX_SAMPLES]` y buffers x/y para el análisis (`int16_t` y float, del mismo tamaño). La corriente se calcula después de la pasada con `ps_current_na` y la calibración del índice de ganancia.
  - **`main.c`:**
    - `store_init` → bus I2C y SPI → `lmp_init`, `ads_init(…, meas_task)` (crear la tarea antes o pasar el handle tras `engine_start`), `ad5662_init` → estado seguro → `engine_start`, `pub_start` → `net_start(on_cmd)`.
    - `on_cmd`: `proto_parse_cmd`. Si hay error → `pub_status("error")`. Según el comando:
      - `stop` → `engine_request_stop`;
      - `run` → `engine_submit` (si está ocupado → error BUSY);
      - `set_profile` / `select_profile` / `set_calibration` → rechazar con BUSY si `engine_busy`; si no, `profile_check` + store, y responder con `status` idle o error;
      - `get_*` → `net_publish("status", proto_write_profile|cal)`.

- [ ] **Step 1: Implementar `engine.c`, `model_hook` y `main.c`.**
- [ ] **Step 2:** `pio run -e esp32-c3-devkitm-1` → SUCCESS, y `pio test -e native` → todos los tests PASS.
- [ ] **Step 3: Escribir `firmware/README.md`** (en español):
  - comandos de build, flash, monitor y tests;
  - configuración con menuconfig (Kconfig);
  - protocolo MQTT con un ejemplo de cada comando;
  - conexionado (incluido el buffer en C1 y el ADS1115 en 0x49);
  - **lista de puesta en marcha del hardware**:
    - (a) celda de 10 kΩ con cada técnica, comparando I contra E/R;
    - (b) osciloscopio en SYNC del AD5662 y ALERT/RDY para comprobar que la conversión termina antes del fin de paso;
    - (c) 20 corridas de CA a 0 V con WiFi conectado contra desconectado: si la desviación estándar difiere más de 2×, pasar a modem-sleep durante la pasada (spec §6);
    - (d) ferricianuro con CV, comparando la forma con KickStat;
    - (e) medir `settle_us` real con un escalón de 100 mV y actualizar el perfil.
- [ ] **Step 4:** Actualizar `CLAUDE.md`: `firmware/` es el proyecto activo; comandos `pio test -e native -f test_<x>` y `pio run -e esp32-c3-devkitm-1`; regla de que `lib/` es puro y `src/` es hardware; `data_takes/` es legacy.
- [ ] **Step 5:** `git commit -m "feat(firmware): motor de medición, despachador y documentación"`
