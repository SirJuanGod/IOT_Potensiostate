#ifndef APP_CONFIG_H
#define APP_CONFIG_H

// ==========================================
// CONFIGURACION CENTRAL DEL POTENCIOSTATO
// Placa  : ESP32-C3 SuperMini
// Rev PCB: 1.1 — pines verificados en esquemático
// ==========================================

// ----- RED Y MQTT -----
#define WIFI_SSID               "MI_RED_WIFI"
#define WIFI_PASSWORD           "MI_PASSWORD"
#define MQTT_BROKER_URL         "mqtt://broker.hivemq.com"
#define MQTT_PUB_TOPIC          "potenciostato/datos"
#define MQTT_SUB_TOPIC          "potenciostato/cmd"
#define WIFI_MAX_RETRY          5

// ----- I2C — ADS1115 -----
#define I2C_MASTER_SDA_IO       8       // GPIO8  → SDA
#define I2C_MASTER_SCL_IO       9       // GPIO9  → SCL
#define I2C_MASTER_FREQ_HZ      400000  // Fast mode 400kHz
#define I2C_MASTER_NUM          0
#define I2C_TIMEOUT_MS          50

// ----- SPI — AD5662 DAC -----
#define SPI_MISO_IO             -1      // AD5662 write-only, sin DOUT
#define SPI_MOSI_IO             5       // GPIO5  → MOSI
#define SPI_SCLK_IO             4       // GPIO4  → SCK
#define SPI_CS_IO               6       // GPIO6  → SYNC (CS activo LOW)
#define SPI_MAX_FREQ_HZ         1000000 // 1MHz — máx recomendado @ 3.3V: 20MHz

// ----- MUX — MAX4558 / ISL84051 -----
#define MUX_SEL_A_IO            1       // GPIO1  → SA
#define MUX_SEL_B_IO            3       // GPIO3  → SB
#define MUX_SEL_C_IO            10      // GPIO10 → SC  ✅ era GPIO2 (strapping)
// Enable conectado a GND físicamente (siempre habilitado)

// ----- ADS1115 -----
#define ADS1115_ADDR            0x48    // ADDR pin a GND
#define ADS1115_REG_CONV        0x00
#define ADS1115_REG_CONFIG      0x01
// OS=1|MUX=100(AIN0)|PGA=001(±4.096V)|MODE=1|DR=100(128SPS)|COMP_QUE=11
#define ADS1115_CONFIG_DEFAULT  0xC383
#define ADC_CONVERSION_TIME_MS  8       // 128SPS → 7.8ms

// ----- AD5662 DAC -----
// Vout_DAC = (D × VREF) / 65536  →  circuito bipolar: ×6 con offset
// code=0→-4.5V | code=32768→≈0V | code=65535→+4.5V (nominal, calibrar)
#define DAC_VREF                1.5f
#define DAC_BIPOLAR_MIN_V       (-4.5f)
#define DAC_BIPOLAR_MAX_V       (4.5f)
#define DAC_CODE_ZERO_V         32768u
#define DAC_MAX_CODE            65535u

// ----- MEDICION -----
// MEASUREMENT_STEP_MS > ADC_CONVERSION_TIME_MS (8ms)
// Velocidad barrido ≈ 50mV / 30ms = 1.67 V/s
#define MEASUREMENT_STEP_MS     10
#define TRIANGULAR_STEP_MV      50

#endif 