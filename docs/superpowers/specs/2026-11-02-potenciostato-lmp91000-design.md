# Potenciostato ESP32-C3 + LMP91000 — Diseño

Fecha: 2026-11-02 · Estado: borrador para aprobación

## 1. Intención

Reescribir desde cero el firmware del potenciostato tomando como referencia KickStat
(LinnesLab/KickStat-Paper-Firmware), pero sobre un **ESP32-C3 SuperMini** con
**LMP91000 + DAC AD5662 + ADC ADS1115**, sin perder la precisión de KickStat.

Para qué: medir voltamogramas cuyas **características** alimentan un modelo de IA que
estima la **concentración** de un compuesto.

- **Etapa 1 (este diseño):** el ESP32 mide y envía por MQTT a una PC los datos crudos,
  las características y los parámetros. En la PC se entrena el modelo.
- **Etapa 2 (futura, fuera de alcance):** el modelo cuantizado corre dentro del ESP32
  y el equipo funciona 100 % portátil. El firmware de la etapa 1 deja preparado ese
  punto de conexión y calcula las características con el mismo código que usará la
  etapa 2.

## 2. Objetivos, no-objetivos, anti-objetivos

**Objetivos**
- Cinco técnicas de KickStat: CV, SWV, DPV, NPV y cronoamperometría (CA), con los
  mismos parámetros.
- Precisión igual o mejor que KickStat: DAC de 16 bits en lugar de 10, y ADC de
  16 bits diferencial en lugar de dos lecturas de 12 bits.
- Límites por compuesto: el firmware rechaza cualquier parámetro fuera de rango.
- Ajuste automático en el ESP32, en dos pasadas (exploración → definitiva), con reglas
  fijas.
- Extracción de características en el ESP32: línea base, corriente de pico, potencial
  de pico y área.
- Envío de todo lo necesario para entrenar: datos crudos por punto, parámetros,
  configuración de hardware, ajustes aplicados, características e identificador de
  muestra.
- ESP-IDF puro con FreeRTOS. Ninguna espera activa ni `delay` bloqueante en la ruta de
  medición.

**No-objetivos** (quedan fuera)
- EIS (el ADS1115, con 860 SPS como máximo, no alcanza).
- El modelo embebido, la pantalla o app, BLE y OTA.
- El script de la PC que recibe y entrena. El protocolo queda documentado para que
  sirva cualquier cliente MQTT.
- Varios LMP91000 y el MUX del proyecto anterior.
- Reutilizar el código de `data_takes/`. Solo se toman de ahí los pines como referencia.

**Anti-objetivos** (fallaría aunque "funcione")
- Perder o redondear datos crudos antes de enviarlos.
- Que la transmisión WiFi ocurra durante una medición e introduzca ruido o jitter.
- Que el modelo reciba mediciones hechas fuera de los límites con que fue entrenado.
- Copiar código de `KickStat.ino`, que no tiene licencia.

## 3. Restricciones

- **Licencias:** las librerías `LMP91000` y `MiniStatAnalyst` de KickStat son MIT; se
  pueden portar conservando su aviso de copyright. `KickStat.ino` no tiene licencia: sus
  algoritmos se reimplementan, no se copian.
- **ESP32-C3:** no tiene DAC ni FPU (el `float` se emula por software; aceptable en la
  etapa 1 y relevante para la etapa 2). Pines de arranque (strapping): GPIO2, 8 y 9.
- **LMP91000** (según su librería y datasheet):
  - Dirección I2C fija **0x48**.
  - VREF externo entre **1,5 V y VDD (3,3 V)**.
  - Bias = signo × {0,1,2,4,6,…,24} % de VREF.
  - Cero interno en 20/50/67 % de VREF.
  - Ganancias del TIA: 2,75k/3,5k/7k/14k/35k/120k/350k, o resistencia externa.
  - RLoad de 10/33/50/100 Ω.
- **Ventana de potencial alcanzable:** |E| ≤ 0,24 × 3,3 V ≈ **792 mV**. Valores
  0 < |E| < 15 mV no se pueden generar (1 % × 1,5 V); E = 0 sí, con bias 0 %. Los
  barridos sí pueden cruzar esa zona: ver §7.
- **ADS1115:** su dirección debe ser **0x49** (ADDR a VDD) para no chocar con el
  LMP91000. 8–860 SPS. PGA de ±0,256 a ±6,144 V.

## 4. Enfoques considerados

**Modelo de IA (decidido: opción 2)**
1. Un modelo por técnica, con la curva remuestreada y los parámetros como entrada.
   Necesita muchos datos variando parámetros.
2. **Elegido: características químicas + parámetros.** Línea base, pico, potencial de
   pico y área, más los parámetros de la medición, van a un modelo pequeño.
   - Pros: generaliza mejor entre configuraciones, necesita menos datos y cabe con
     holgura en el C3.
   - Encaja con los límites por compuesto y con el ajuste automático para mejorar la
     resolución.
3. Un único modelo para todas las técnicas. Descartado: necesita demasiados datos y es
   difícil de embeber.

**Framework (decidido: ESP-IDF puro)**
- Seguir el proyecto base ESP-IDF: descartado por el usuario, que prefiere reescribir
  con solo LMP91000 + DAC + ADC.
- Arduino sobre ESP32-C3: más fácil de portar desde KickStat, pero el usuario eligió
  ESP-IDF con FreeRTOS y esperas no bloqueantes.
- **Elegido: ESP-IDF 5.5 puro**, proyecto nuevo en `firmware/` con PlatformIO
  (`framework = espidf`), igual que el proyecto anterior.

**Ajuste de resolución (decidido: automático en el ESP32, con reglas fijas, en dos
pasadas).** Es mejor que cambiar la ganancia a mitad del barrido, porque eso deforma la
curva. KickStat también usa ganancia fija por medición.

## 5. Hardware

```
ESP32-C3 ──SPI──► AD5662 ──Vout──► LMP91000.VREF (1,5–3,3 V)
    │                                   │ WE/RE/CE ── celda
    ├──I2C 0x48──► LMP91000 (config)    │
    ├──I2C 0x49──► ADS1115 ◄── AIN0 = LMP.VOUT, AIN1 = buffer(LMP.C1) (diferencial)
    ├──GPIO ◄──── ADS1115 ALERT/RDY (interrupción de conversión lista)
    └──GPIO ────► LMP91000 MENB (activo bajo; puede ir fijo a GND)
```

- **AD5662:** su salida va **directa** a VREF del LMP91000. Ya no se usa el circuito
  bipolar de ±4,5 V del proyecto anterior.
  - La exactitud del potencial depende de la referencia del AD5662. Se recomienda una
    referencia de voltaje dedicada (p. ej. 3,0 V o 3,3 V de precisión) en lugar del
    riel de 3,3 V.
  - Su valor se configura (`DAC_VREF_MV`) y se calibra.
- **Buffer en C1** (decidido con el usuario; la placa aún no existe). C1 es el nodo
  inversor del TIA. Conectar el ADS1115 directo lo cargaría con unos pocos MΩ y
  metería una corriente de error que cambia con VREF. Se agrega un seguidor de voltaje
  entre C1 y AIN1.
  - Requisitos del op-amp: CMOS, entrada y salida rail-to-rail a 3,3 V, Ib < 100 pA
    (esa corriente entra al nodo del TIA) y Vos bajo, porque entra directo en
    VOUT−C1. Un Vos constante se corrige con la calibración de offset.
  - Candidato a verificar en la fase de esquemático: un op-amp zero-drift tipo OPA333.
  - Con el buffer, la calibración por equipo (§12) vuelve a ser un offset y una
    ganancia por ganancia del TIA. Ya no se necesita por combinación con el PGA.
- **Pines propuestos** (decisión del diseñador, a confirmar con el PCB): SPI
  MOSI=GPIO5, SCLK=GPIO4, CS=GPIO6; I2C SDA=GPIO8, SCL=GPIO9 (pull-ups externos de
  4,7 kΩ, que además dejan los pines de arranque en el nivel correcto); ADS1115
  RDY=GPIO10; MENB=GPIO3.
- **Celda de prueba:** una resistencia conocida (p. ej. 10 kΩ) entre WE y RE/CE da
  I = E/R esperado, para validar.

## 6. Arquitectura del firmware

ESP-IDF 5.5.x, `CONFIG_FREERTOS_HZ=1000`. Se usa el **nuevo** driver I2C
(`driver/i2c_master.h`); no hay código legacy que obligue a usar el viejo. Credenciales
WiFi, URI del broker e ID de dispositivo van en **Kconfig** (menuconfig), no en el
código.

### Módulos

Los módulos marcados como *puro* no incluyen cabeceras de ESP-IDF y se compilan en la
PC para tests.

| Módulo | Responsabilidad |
|---|---|
| `lmp91000` | Port en C de la librería MIT: registros, lock/unlock, ganancia, RLoad, referencia externa, cero interno, bias y signo, modo |
| `ad5662` | Trama SPI de 24 bits `[PD1:PD0][D15..D0]`, modo 1 |
| `ads1115` | Configura MUX diferencial AIN0−AIN1, PGA y DR; dispara single-shot; RDY por interrupción → notificación a la tarea |
| `potential` *(puro)* | Resuelve E → (signo, índice de bias, código DAC) y devuelve el potencial realmente aplicado (§7) |
| `technique` *(puro)* | Genera la secuencia de pasos de cada técnica a partir de sus parámetros (§8) |
| `engine` | Tarea de medición: ejecuta los pasos con temporización precisa y guarda los puntos |
| `analysis` *(puro)* | Línea base, pico, potencial de pico y área (reimplementa la idea de MiniStatAnalyst, MIT) |
| `autorange` *(puro)* | Reglas de la exploración → parámetros de la definitiva (§10) |
| `profile` | Límites y ventanas de análisis por compuesto; validación; persistencia en NVS (§9) |
| `net` | WiFi STA con reconexión + cliente esp-mqtt |
| `protocol` *(puro en lo posible)* | Parseo de comandos y serialización JSON en bloques (§11) |
| `model_hook` | Interfaz vacía `features → concentración` para la etapa 2 |

### Tareas y sincronización (sin bloqueos)

- **`meas_task`** (la prioridad más alta de la aplicación). Espera comandos en una cola.
  Durante una medición se bloquea solo en `ulTaskNotifyTake`. La despiertan:
  - un `esp_timer` one-shot que marca el fin de cada paso y el instante de disparo
    del ADC;
  - la interrupción RDY del ADS1115.

  No hay `while(millis()…)` ni `vTaskDelay` para tiempos de medición: la resolución es
  de µs (esp_timer), no de ticks.
- **`net`.** esp-mqtt corre en su propia tarea. Los comandos recibidos se validan y se
  encolan a `meas_task`.
- **`pub_task`** (baja prioridad). Recibe de `meas_task` el aviso de "pasada terminada"
  y publica los bloques desde el buffer.
  - **Nunca se publica durante una pasada**, para que la transmisión WiFi no meta
    ruido. Se publica entre la exploración y la definitiva, y al final.
  - *Decisión del diseñador:* la conexión sigue activa durante la pasada, con el
    keepalive de MQTT y la recepción de `stop`. Solo se retiene la publicación.
    Apagar la radio rompería el `stop` remoto. La prueba en hardware (§13) mide el
    ruido con WiFi conectado; si se ve en los datos, se pasa a modem-sleep durante la
    pasada.
- `stop` llega por MQTT. `meas_task` lo atiende en el siguiente evento, pone la celda
  en estado seguro (bias 0 %, VREF válido) y publica lo medido marcado como
  `aborted`.

### Temporización de un paso

Cada paso es `{E_mV, dur_us, sample, tag}`. Al iniciar el paso:
1. Se aplica E con las escrituras I2C al LMP91000 y SPI al AD5662.
2. Si el paso muestrea, se programa el disparo del ADS1115 en
   `t_fin − t_conv(DR) − margen`, para que la lectura caiga al **final del paso**,
   como hace KickStat.
3. Se elige el **DR más lento** cuyo tiempo de conversión quepa en el paso: menos
   ruido. Opcionalmente se promedian N conversiones si caben.
4. Se guarda `t_us` real de la conversión.

Si un paso es más corto que la conversión más rápida posible más las escrituras, el
perfil o la validación lo rechazan.

## 7. Resolución del potencial (reimplementa `setVoltage()` de KickStat)

E_celda = signo × VREF × bias%, con VREF = código × DAC_VREF / 65536, en [1500, 3300] mV.

Para un E pedido:
- **E = 0:** bias 0 %, VREF en un valor válido.
- **0 < |E| < 15 mV** *(decisión del diseñador)*: se aplica el valor alcanzable más
  cercano: 0 si |E| < 7,5 mV, ±15 mV si no. El valor real queda en `E_apl` y el punto
  se marca con el flag `snapped`. No se rechaza la medición: casi todo barrido cruza
  0 V. KickStat hace lo mismo.
- **Resto:** para cada índice de bias 1..13 se calcula VREF = |E| / bias%. Se descartan
  los que caen fuera de [1500, 3300] mV. Se redondea el código DAC y se elige la
  combinación con **menor error** |E_aplicado − E|.
  - En empate, gana la que tiene mayor VREF, para tener más señal en el TIA.

Se devuelven `signo`, `bias_idx`, `dac_code` y `E_aplicado_mV`, y los cuatro se guardan
por punto. Con el DAC de 16 bits el error típico es mucho menor que la tolerancia de
0,8 % de KickStat.

Corrige dos bugs de KickStat: acceso `TIA_BIAS[14]` fuera del arreglo y división entre
cero con E = 0.

Al cambiar `bias_idx` o VREF entre puntos hay un transitorio. Se respeta un
`settle_us` mínimo, configurable en el perfil, antes de muestrear.

## 8. Técnicas

Mismos parámetros y semántica que KickStat. Potenciales en mV y tiempos en ms en la
interfaz (µs internamente). Cada paso muestreado guarda su `tag`.

| Técnica | Parámetros | Pasos |
|---|---|---|
| CV | `cycles, start, end, vertex1, vertex2, step, rate_mV_s` | Barrido escalonado start→v1→v2→end por ciclo; dur = step/rate; sin repetir vértices; tag = ciclo y rama |
| SWV | `start, end, amp, step, freq_hz` | Por escalón E: paso E+amp (`fwd`) y E−amp (`rev`), cada uno de 1/(2f); punto = i_fwd − i_rev |
| DPV | `start, end, amp, step, period_ms, width_ms` | Por escalón E: base E durante period−width (`base`), pulso E+amp durante width (`pulse`); punto = i_pulse − i_base |
| NPV | `start, end, amp, width_ms, period_ms, quiet_ms` | Reposo en start durante quiet; por escalón: pulso a E durante width (`pulse`), vuelta a start durante period−width (`base`) |
| CA | `pre_step, quiet_ms, v1, t1_ms, v2, t2_ms, samples` | Tres segmentos {pre_step, v1, v2} con `samples` muestras equiespaciadas en cada uno |

Las dos corrientes de SWV/DPV/NPV se guardan **por separado**, además de la
diferencia. KickStat solo guarda un ciclo de CV; aquí se guardan todos, con el número
de ciclo.

## 9. Perfil de compuesto y límites

Un perfil con nombre, guardado en NVS (se permiten varios y uno está activo):
- **Límites:** `E_min, E_max`, `step_min/max`, `amp_max`, `freq_min/max`,
  `rate_min/max`, ganancias del TIA permitidas, RLoad, `R_ext` si se usa ganancia
  externa, `settle_us`, `max_points`.
- **Análisis:** `redox` (+1 oxidación / −1 reducción), ventana de búsqueda de pico,
  dos ventanas de línea base. Las ventanas van en mV absolutos, como en
  MiniStatAnalyst.

**Características por técnica** *(decisión del diseñador)*. El código es el mismo en
la etapa 1 y en la etapa 2. Como los crudos se envían, la PC puede recalcular otras
características si hacen falta.
- **SWV, DPV y NPV:** sobre la curva de diferencia.
- **CV:** sobre el último ciclo. Si `redox` = +1 se usa la rama anódica (hacia
  potenciales positivos) y si es −1 la catódica. Un pico por medición.
- **CA:** no hay pico. Las características son la corriente media del último 20 % de
  cada segmento y la carga integrada de cada segmento.
- **Defaults:** técnica y parámetros por defecto, para el modo portátil futuro.

Validación: todo comando `run` se valida contra el perfil activo **y** contra las
restricciones físicas (§3). Si algo se sale, se rechaza con un error que dice qué
campo y qué límite. Nunca se recorta en silencio. El perfil se fija y se lee por MQTT.

## 10. Ajuste automático (dos pasadas)

Si `auto=true`:
1. **Exploración:** la misma técnica sobre la ventana completa permitida por el
   perfil, con paso grueso (decisión del diseñador: 4× el paso pedido, dentro de los
   límites) y la ganancia por defecto.
2. **Reglas** sobre la exploración, en orden. Los umbrales van en el perfil, con estos
   valores iniciales:
   - **Saturación:** algún |código ADC| > 95 % de la escala → bajar PGA y, si ya está
     al mínimo, bajar la ganancia del TIA.
   - **Señal pequeña:** pico corregido < 20 % de la escala → subir PGA y después la
     ganancia del TIA, sin pasar del 80 % proyectado.
   - **Pico en el borde:** dentro del 10 % del borde de la ventana → recentrar la
     ventana en el pico, sin salir de los límites.
   - **Pico bien ubicado:** ventana = pico ± k × ancho a media altura (k = 3), paso
     final = el pedido por el usuario o el mínimo del perfil.
3. **Definitiva** con los parámetros resultantes. Se extraen las características y se
   envía el informe de ajustes (qué regla se aplicó y valores antes → después).

Si la exploración no encuentra pico, la definitiva se hace con los parámetros pedidos
y se marca `no_peak`. Hay como máximo **una** definitiva; no hay bucles.

*Decisiones del diseñador:*
- La ventana final se **amplía siempre** para incluir las ventanas de línea base y de
  búsqueda de pico del perfil. Así las características se calculan igual en todas las
  muestras.
- En **CA** el ajuste automático solo aplica las reglas de ganancia (saturación y
  señal pequeña), porque no hay ventana de potencial.

## 11. Protocolo MQTT

Topics `potenciostato/<device_id>/{cmd,status,data,result}`, QoS 1.

**Comandos (`cmd`)**
```json
{"cmd":"run","id":"muestra_07","tec":"swv","auto":true,
 "params":{"start":-400,"end":500,"amp":50,"step":1,"freq_hz":31.25},
 "gain":"auto"}
{"cmd":"stop"}
{"cmd":"set_profile","profile":{...}}   {"cmd":"get_profile"}   {"cmd":"select_profile","name":"..."}
{"cmd":"set_calibration","cal":{...}}   {"cmd":"get_calibration"}
```

*Decisión del diseñador:* la calibración la calcula la PC (por ejemplo, con mediciones
sobre la celda de resistencia) y la envía con `set_calibration`. El firmware solo la
guarda en NVS y la aplica; no tiene un modo de calibración propio.

**Datos (`data`)**, por pasada:
- Un mensaje de cabecera con `id`, `pass` (`explore`/`final`), técnica, parámetros
  efectivos, perfil, ganancia y RLoad, PGA, DR, `DAC_VREF_MV`, versión de firmware y
  `t0`.
- Bloques de ≤ ~4 KB:
  ```
  {"id","pass","seq","last","rows":[[t_us,tag,E_set,E_apl,sign,bias_idx,dac_code,adc_raw,pga,i_nA], ...]}
  ```
- Los crudos (`dac_code`, `adc_raw`, `bias_idx`, `pga`) van siempre. `i_nA` es
  derivado.

**Resultado (`result`)**
```json
{"id","features":{"baseline_m","baseline_b","ip_nA","Ep_mV","area_nC"},"adjustments":[...],"flags":["no_peak"|"aborted"|...]}
```

**Estado (`status`)**: `idle`, `running`, `error` con motivo, más un último deseo (LWT)
`offline`.

El `id` lo pone la PC y el ESP32 solo lo devuelve. La concentración real de cada
muestra la asocia la PC.

**Memoria:** los puntos se guardan en RAM hasta publicar. Con `max_points` = 4000 y
~20 B/punto son ~80 KB, que caben en los ~400 KB del C3.

## 12. Cálculo de corriente

I = (V_out − V_C1) / R_TIA, con V_out − V_C1 = `adc_raw` × LSB(PGA) medido en
diferencial. R_TIA sale de la tabla de ganancias o de `R_ext` del perfil (RFB de
KickStat: 2,2 MΩ).

Calibración: offset y ganancia por equipo, medidos con la celda de resistencia y
guardados en NVS. Se aplican a `i_nA`, nunca a los crudos.

## 13. Pruebas

- **En la PC** (PlatformIO `env:native` + Unity) para los módulos puros:
  - `potential`: error dentro de tolerancia en toda la ventana, E = 0, rechazo de
    |E| < 15 mV, sin índices fuera de rango.
  - `technique`: número de pasos, vértices sin duplicar y tiempos.
  - `analysis`: pico y línea base en curvas sintéticas.
  - `autorange`: cada regla.
  - `protocol`: validación contra límites y formato de bloques.
- **En hardware:**
  - celda de resistencia con cada técnica, I medida vs. E/R;
  - comprobar con un osciloscopio que el disparo del ADC cae al final del paso;
  - ferricianuro con CV, como en KickStat, para comparar la forma.

## 14. Lo que queda al constructor

Nombres exactos de archivos y componentes, tamaños de stack y prioridades concretas,
tamaño de bloque, margen de disparo del ADC, N de promedio, formato exacto de errores y
el valor por defecto de `settle_us` (medirlo en hardware).
