# Potenciostato ESP32-C3 Super Mini

Potenciostato basado en ESP32-C3 Super Mini para voltametria ciclica (CV). Aplica un barrido de voltaje triangular a una celda electroquimica mediante un DAC (AD5662) y mide la corriente resultante con un ADC (ADS1115). Los resultados se publican via MQTT sobre WiFi para monitoreo y analisis remoto.

## Tabla de contenidos

- [Hardware](#hardware)
- [Arquitectura de software](#arquitectura-de-software)
- [Configuracion de pines](#configuracion-de-pines)
- [Flujo de medicion](#flujo-de-medicion)
- [Comunicacion MQTT](#comunicacion-mqtt)
- [Requisitos](#requisitos)
- [Compilar y flashear](#compilar-y-flashear)
- [Uso](#uso)
- [Estructura del proyecto](#estructura-del-proyecto)
- [Parametros de configuracion](#parametros-de-configuracion)

## Hardware

| Componente | Modelo | Funcion | Interfaz |
|---|---|---|---|
| Microcontrolador | ESP32-C3 SuperMini | Procesador principal | - |
| DAC | AD5662 | Genera voltaje de barrido (16 bits) | SPI |
| ADC | ADS1115 | Mide corriente via TIA | I2C |
| Multiplexor | MAX4558 | Seleccion de canal (8 canales) | GPIO |

### Diagrama de bloques

```
ESP32-C3
  |
  |-- SPI2 --> AD5662 DAC --> Voltaje de salida (-4.5V a +4.5V)
  |                               |
  |                         Celda electroquimica
  |                               |
  |                         Amplificador de transimpedancia (TIA)
  |                               |
  |-- I2C0 --> ADS1115 ADC --> Medicion de corriente (+-4.096V)
  |
  |-- GPIO --> MAX4558 MUX --> Seleccion de canal (0-7)
  |
  |-- WiFi --> MQTT Broker --> Publicacion de datos / comandos
```

## Arquitectura de software

El firmware esta organizado en modulos independientes bajo `lib/`:

```
src/main.c                    Punto de entrada, secuencia de arranque
  |
  |-- potenciostato_core      Motor de medicion (maquina de estados + FreeRTOS)
  |     |-- spi_driver        Control del DAC AD5662
  |     |-- i2c_driver        Control del ADC ADS1115
  |     |-- mux_driver        Control del multiplexor MAX4558
  |
  |-- wifi_manager            Conexion WiFi (modo estacion)
  |-- mqtt_app                Cliente MQTT (publicacion de datos y recepcion de comandos)
  |-- app_config              Constantes de configuracion centralizadas
```

### Descripcion de modulos

| Modulo | Archivo | Descripcion |
|---|---|---|
| **Core** | `lib/core/potenciostato_core.c` | Maquina de estados de 3 pasos para el barrido triangular. Usa un timer periodico + semaforo binario + tarea FreeRTOS. |
| **SPI Driver** | `lib/spi_driver/spi_driver.c` | Inicializa SPI2 (modo 1, CPOL=0 CPHA=1). Envia tramas de 24 bits al AD5662. |
| **I2C Driver** | `lib/i2c_driver/i2c_driver.c` | Inicializa I2C en modo Fast (400 kHz). Lectura/escritura de registros del ADS1115. |
| **MUX Driver** | `lib/mux_driver/mux_driver.c` | Controla 3 GPIOs para seleccionar entre 8 canales del MAX4558. |
| **WiFi Manager** | `lib/wifi_manager/wifi_manager.c` | Conexion WiFi STA con reconexion automatica (hasta 5 reintentos). |
| **MQTT** | `lib/mqtt_driver/mqtt_app.c` | Cliente MQTT con soporte para mensajes fragmentados, mutex y callbacks. |
| **Config** | `lib/config/app_config.h` | Pines, frecuencias, credenciales WiFi, URL del broker, parametros de medicion. |

## Configuracion de pines

### I2C (ADS1115)

| Senal | GPIO |
|---|---|
| SDA | 8 |
| SCL | 9 |

- Frecuencia: 400 kHz
- Direccion ADS1115: 0x48

### SPI (AD5662)

| Senal | GPIO |
|---|---|
| MOSI | 5 |
| SCLK | 4 |
| CS (SYNC) | 6 |

- Frecuencia: 1 MHz
- Modo SPI: 1 (CPOL=0, CPHA=1)
- Sin MISO (DAC solo escritura)

### MUX (MAX4558)

| Senal | GPIO |
|---|---|
| SEL_A | 1 |
| SEL_B | 3 |
| SEL_C | 10 |

- Pin ENABLE conectado a GND (siempre habilitado)
- Seleccion de canal por codificacion binaria (A=bit0, B=bit1, C=bit2)

## Flujo de medicion

El motor de medicion implementa una maquina de estados de 3 pasos sincronizada por un timer periodico de alta resolucion (`esp_timer`):

```
Timer (periodo = 10 ms)
  |
  v
Semaforo binario --> Despierta meas_task (prioridad 5)
  |
  v
Maquina de estados:

  STEP_WRITE_DAC
    Convierte voltaje (mV) a codigo DAC de 16 bits
    Envia al AD5662 via SPI
    |
    v
  STEP_START_ADC
    Escribe registro CONFIG del ADS1115 (single-shot, OS=1)
    Inicia conversion (~8 ms a 128 SPS)
    |
    v
  STEP_READ_ADC
    Lee registro de conversion del ADS1115 (16 bits, big-endian)
    Convierte a corriente: I(uA) = (raw * 0.125mV) / R_SHUNT * 1000
    Almacena punto {voltaje, raw, corriente}
    Avanza voltaje += 50 mV * direccion
    Si alcanza +4500 mV: invierte direccion
    Si alcanza -4500 mV: finaliza medicion
    |
    v
  Vuelve a STEP_WRITE_DAC
```

### Parametros del barrido triangular

- **Rango de voltaje:** -4500 mV a +4500 mV
- **Paso de voltaje:** 50 mV
- **Intervalo entre pasos:** 10 ms (3 pasos por punto = 30 ms/punto)
- **Puntos por barrido:** ~360-400
- **Velocidad de barrido:** ~5 V/s

## Comunicacion MQTT

### Broker

Por defecto: `mqtt://broker.hivemq.com`

### Topics

| Topic | Direccion | Descripcion |
|---|---|---|
| `potenciostato/datos` | Publicacion (QoS 1) | Resultados de medicion en JSON |
| `potenciostato/cmd` | Suscripcion | Comandos de control |

### Formato de datos (JSON)

```json
{
  "puntos": 400,
  "datos": [
    {"v": -4500.0, "i": 0.123},
    {"v": -4450.0, "i": 0.456},
    ...
  ]
}
```

- `v`: voltaje aplicado en mV
- `i`: corriente medida en uA

### Comandos

| Comando | Accion |
|---|---|
| `start` | Inicia un barrido de voltametria ciclica |
| `stop` | Detiene la medicion en curso |

## Requisitos

- [PlatformIO](https://platformio.org/) (CLI o extension de VS Code)
- ESP32-C3 DevKitM-1 o compatible
- Hardware externo: AD5662, ADS1115, MAX4558

## Compilar y flashear

```bash
# Compilar
pio run -e esp32-c3-devkitm-1

# Flashear
pio run -e esp32-c3-devkitm-1 --target upload

# Monitor serial
pio device monitor
```

## Uso

1. Configurar credenciales WiFi y URL del broker MQTT en `lib/config/app_config.h`
2. Compilar y flashear el firmware
3. El dispositivo se conecta automaticamente a WiFi y al broker MQTT
4. Enviar `start` al topic `potenciostato/cmd` para iniciar una medicion
5. Los resultados se publican en `potenciostato/datos` al finalizar el barrido
6. Enviar `stop` para detener una medicion en curso

## Estructura del proyecto

```
Potensiostate/
|-- src/
|   |-- main.c                    Punto de entrada
|   |-- CMakeLists.txt
|-- lib/
|   |-- config/
|   |   |-- app_config.h          Configuracion centralizada
|   |-- core/
|   |   |-- potenciostato_core.c  Motor de medicion
|   |   |-- potenciostato_core.h
|   |-- spi_driver/
|   |   |-- spi_driver.c          Driver SPI (AD5662)
|   |   |-- spi_driver.h
|   |-- i2c_driver/
|   |   |-- i2c_driver.c          Driver I2C (ADS1115)
|   |   |-- i2c_driver.h
|   |-- mux_driver/
|   |   |-- mux_driver.c          Driver MUX (MAX4558)
|   |   |-- mux_driver.h
|   |-- mqtt_driver/
|   |   |-- mqtt_app.c            Cliente MQTT
|   |   |-- mqtt_app.h
|   |-- wifi_manager/
|   |   |-- wifi_manager.c        Gestion WiFi
|   |   |-- wifi_manager.h
|-- include/
|   |-- pin_out/
|       |-- pinout.txt
|-- platformio.ini                Configuracion PlatformIO
|-- CMakeLists.txt                Configuracion CMake
|-- diagram.json                  Diagrama Wokwi (simulacion)
|-- wokwi.toml                    Config Wokwi
```

## Parametros de configuracion

Todos los parametros se definen en `lib/config/app_config.h`:

| Parametro | Valor | Descripcion |
|---|---|---|
| `MEASUREMENT_STEP_MS` | 10 | Periodo del timer de medicion (ms) |
| `TRIANGULAR_STEP_MV` | 50 | Incremento de voltaje por paso (mV) |
| `DAC_BIPOLAR_MIN_V` | -4.5 | Voltaje minimo de salida (V) |
| `DAC_BIPOLAR_MAX_V` | +4.5 | Voltaje maximo de salida (V) |
| `DAC_MAX_CODE` | 65535 | Codigo maximo del DAC (16 bits) |
| `I2C_MASTER_FREQ_HZ` | 400000 | Frecuencia I2C (Hz) |
| `SPI_MAX_FREQ_HZ` | 1000000 | Frecuencia SPI (Hz) |
| `MAX_MEAS_POINTS` | 400 | Maximo de puntos por barrido |
| `R_SHUNT_OHMS` | 1000 | Resistencia shunt del TIA (ohms) |
| `WIFI_MAX_RETRY` | 5 | Reintentos de conexion WiFi |
| `WIFI_CONNECT_TIMEOUT_MS` | 10000 | Timeout de conexion WiFi (ms) |

### Formulas de conversion

**DAC (voltaje a codigo):**
```
code = (V_mV - V_min) / (V_max - V_min) * 65535
```

**ADC (raw a corriente):**
```
V_adc = raw * 0.125 mV/LSB     (PGA = +-4.096V)
I_uA  = (V_adc / R_SHUNT) * 1000
```
