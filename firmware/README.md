# Firmware potenciostato (LMP91000 + AD5662 + ADS1115, ESP32-C3)

Potenciostato para voltamperometría (CV, SWV, DPV, NPV) y cronoamperometría (CA). El AD5662
entrega el VREF externo del LMP91000, el LMP91000 fija el potencial de celda (signo + % de bias)
y convierte la corriente con su TIA, y el ADS1115 mide en diferencial `VOUT − C1`. Los
resultados se publican en JSON por MQTT. Diseño completo:
`docs/superpowers/specs/2026-11-02-potenciostato-lmp91000-design.md`.

## Comandos

Desde `firmware/`:

```bash
pio test -e native                         # tests de las libs puras en la PC (Unity)
pio test -e native -f test_protocol        # un solo módulo
pio run -e esp32-c3-devkitm-1              # build del equipo
pio run -e esp32-c3-devkitm-1 -t upload    # flash
pio device monitor                         # log serie (115200)
pio run -e esp32-c3-devkitm-1 -t menuconfig
```

- `platform = espressif32@6.13.0` fija ESP-IDF 5.5.3 (sin fijar resolvía a IDF 6.1, donde
  `mqtt` ya no es componente del core).
- Partición `partitions_singleapp_large.csv` (app de 1,5 MB en flash de 2 MB).
- `sdkconfig.esp32-c3-devkitm-1` se genera desde `sdkconfig.defaults` y no se versiona.

## Configuración (menuconfig → "Potenciostato")

| Opción | Uso |
|---|---|
| `PS_WIFI_SSID`, `PS_WIFI_PASSWORD` | Red WiFi (STA, reconexión con backoff 1…30 s) |
| `PS_MQTT_URI` | Broker, p. ej. `mqtt://192.168.1.10` |
| `PS_DEVICE_ID` | Prefijo de topics: `potenciostato/<device_id>/…` |
| `PS_DAC_VREF_MV` | Referencia real del AD5662 en mV (3000–3300); medirla y ajustarla |

Las credenciales nunca van en el código.

## Organización

- `lib/` es **puro**: no incluye cabeceras de ESP-IDF ni de FreeRTOS y se prueba en la PC
  (`ps_common`, `potential`, `technique`, `profile`, `analysis`, `autorange`, `protocol`,
  `hwregs`, `model_hook`). Integración: `lib/` plano de PlatformIO, sin `EXTRA_COMPONENT_DIRS`.
- `src/` es **hardware**: drivers (`lmp91000`, `ad5662`, `ads1115`), `store` (NVS), `net`
  (WiFi + MQTT), `pub` (tarea de publicación), `engine` (tarea de medición) y `main`.

### Tareas

| Tarea | Prioridad | Función |
|---|---|---|
| `meas_task` | 10 | Ejecuta los runs. Entre pasos solo se bloquea en su notificación (índice 0): `esp_timer` one-shot marca el disparo del ADC y el fin de cada paso a tiempo absoluto (`t0 + Σdur`), y la interrupción ALERT/RDY avisa la conversión lista. Sin `vTaskDelay` ni esperas activas dentro de una pasada. |
| esp-mqtt | 5 | Recibe comandos y llama a `on_cmd` (`main.c`), que solo valida, guarda en NVS o encola. |
| `pub_task` | 3 | Publica una pasada terminada en bloques de ≤ 4 KB; avisa a `meas_task` en el índice 1. Nunca publica durante una pasada. |

Un run: validación contra el perfil activo → (si `auto`) pasada de exploración, publicación y
reglas de ajuste → pasada definitiva → resultado. Después de cada pasada la celda queda en
estado seguro (bias 0 %, VREF = 2000 mV, LMP bloqueado) y al final se publica `idle`.

## Protocolo MQTT

Topics `potenciostato/<device_id>/{cmd,status,data,result}`, QoS 1. `status` lleva LWT
`{"state":"offline"}` y `idle` retenido.

Comandos en `cmd`:

```json
{"cmd":"run","id":"muestra_07","tec":"swv","auto":true,"gain":"auto",
 "params":{"start":-400,"end":500,"amp":25,"step":2,"freq_hz":25}}
{"cmd":"run","id":"cv1","tec":"cv","gain":3,
 "params":{"cycles":2,"start":0,"vertex1":500,"vertex2":-500,"end":0,"step":5,"rate_mV_s":100}}
{"cmd":"run","id":"dpv1","tec":"dpv","params":{"start":-400,"end":400,"amp":50,"step":5,"period_ms":200,"width_ms":50}}
{"cmd":"run","id":"npv1","tec":"npv","params":{"start":-400,"end":400,"amp":10,"width_ms":50,"period_ms":200,"quiet_ms":1000}}
{"cmd":"run","id":"ca1","tec":"ca","params":{"pre_step":0,"quiet_ms":1000,"v1":-200,"t1_ms":2000,"v2":300,"t2_ms":2000,"samples":50}}
{"cmd":"stop"}
{"cmd":"set_profile","profile":{"name":"default","e_min_mv":-600,"e_max_mv":600,"step_min_mv":1,"step_max_mv":20,
  "amp_max_mv":100,"freq_min_hz":1,"freq_max_hz":100,"rate_min_mv_s":5,"rate_max_mv_s":500,"gain_mask":127,
  "default_gain_idx":3,"rload_idx":0,"r_ext_ohm":0,"settle_us":500,"max_points":4000,"redox":1,
  "peak_win":[-600,600],"base_win1":[-600,-500],"base_win2":[500,600],"sat_pct":95,"small_pct":20,
  "edge_pct":10,"target_pct":80,"fwhm_k":3,"defaults":{"tec":"swv","params":{"start":-400,"end":500,"amp":25,"step":2,"freq_hz":25}}}}
{"cmd":"select_profile","name":"default"}
{"cmd":"get_profile"}
{"cmd":"set_calibration","cal":{"offset_na":[0,0,0,0,0,0,0,0],"gain":[1,1,1,1,1,1,1,1]}}
{"cmd":"get_calibration"}
```

- `gain`: `"auto"` (o ausente) usa `default_gain_idx` del perfil; 0..6 son las ganancias
  internas del TIA (2,75 k…350 kΩ) y 7 la resistencia externa `r_ext_ohm`.
- Todo `run` se valida contra el perfil activo y los límites físicos; si algo se sale se
  responde en `status` con `{"state":"error","code":"range","field":…,"limit":…,"msg":…}`.
  Nunca se recorta en silencio.
- Con un run en curso, cualquier comando salvo `stop` responde `code:"busy"`.
- `get_profile` / `get_calibration` responden en `status` con el JSON del perfil activo o de
  la calibración. `set_*` y `select_profile` responden `idle` o `error`.
- La calibración la calcula la PC (offset en nA y ganancia por índice de ganancia 0..7) y se
  aplica solo a `i_nA`, nunca a los crudos.

Salida de cada pasada: cabecera en `data` (parámetros efectivos, perfil, ganancia, RLoad, PGA,
`dac_vref_mv`, `t0_us`, `fw_version`, `cols`), bloques de filas en `data`
(`[t_us,tag,cycle,branch,x_mV,E_set_mV,E_apl_uV,sign,bias_idx,dac_code,adc_raw,pga,dr,i_nA,flags]`,
`t_us` relativo a `t0_us`) y, en la última pasada, `result` con `features`, `adjustments` y
`flags` (`no_peak`, `peak_at_edge`, `aborted`, `error`). `status` pasa a `running` al empezar
cada pasada y a `idle` al terminar el run.

## Conexionado

```
ESP32-C3 ──SPI──► AD5662 ──Vout──► LMP91000.VREF (1,5–3,3 V)
    ├──I2C 0x48──► LMP91000 (config)
    ├──I2C 0x49──► ADS1115 ◄── AIN0 = LMP.VOUT, AIN1 = buffer(LMP.C1)
    ├──GPIO10 ◄─── ADS1115 ALERT/RDY
    └──GPIO3 ───► LMP91000 MENB (activo bajo; puede ir fijo a GND)
```

| Señal | GPIO |
|---|---|
| SPI MOSI / SCLK / CS (AD5662, modo 1, 24 bits) | 5 / 4 / 6 |
| I2C SDA / SCL (400 kHz) | 8 / 9 |
| ADS1115 ALERT/RDY | 10 |
| LMP91000 MENB | 3 |

- **La placa debe tener el buffer en C1.** C1 es el nodo inversor del TIA; conectarlo directo
  al ADS1115 lo carga con unos MΩ y mete un error que cambia con VREF. Se pone un seguidor de
  voltaje entre C1 y AIN1: op-amp CMOS, entrada y salida rail-to-rail a 3,3 V, Ib < 100 pA y Vos
  bajo (candidato: OPA333). Un Vos constante se corrige con el offset de calibración.
- **ADS1115 en 0x49:** ADDR a VDD (0x48 es del LMP91000, fija). ALERT/RDY necesita pull-up
  (el firmware activa el interno como respaldo).
- I2C con pull-ups externos de 4,7 kΩ (los internos quedan solo de respaldo).
- Se recomienda una referencia de precisión para el AD5662 (3,0 o 3,3 V) en vez del riel de
  3,3 V; su valor real va en `PS_DAC_VREF_MV`.

## Puesta en marcha del hardware

- [ ] **(a) Celda de 10 kΩ** entre WE y RE/CE: correr cada técnica (CV, SWV, DPV, NPV, CA) y
  comparar `i_nA` con E/R. De aquí salen el offset y la ganancia de calibración por índice de
  ganancia (`set_calibration`).
- [ ] **(b) Osciloscopio** en SYNC del AD5662 (CS, GPIO6) y en ALERT/RDY (GPIO10): el flanco de
  RDY debe caer antes del siguiente SYNC, es decir, la conversión termina antes del fin de paso.
- [ ] **(c) Ruido de la radio:** 20 corridas de CA a 0 V con WiFi conectado y otras 20 con WiFi
  desconectado. Si la desviación estándar difiere más de 2×, pasar a modem-sleep durante la
  pasada (spec §6).
- [ ] **(d) Ferricianuro con CV:** comparar la forma del voltamograma con la de KickStat.
- [ ] **(e) `settle_us` real:** aplicar un escalón de 100 mV, medir cuánto tarda en asentarse la
  salida y actualizar `settle_us` en el perfil (`set_profile`).
