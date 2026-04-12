#ifndef APP_CONFIG_H
#define APP_CONFIG_H

// ==========================================
// CONFIGURACION CENTRAL DEL POTENCIOSTATO
// Placa: ESP32-C3 Super Mini
// ==========================================

// ----- RED Y MQTT -----
#define WIFI_SSID           "MI_RED_WIFI"
#define WIFI_PASS           "MI_PASSWORD"
#define MQTT_BROKER_URL     "mqtt://broker.hivemq.com"
#define MQTT_PUB_TOPIC      "potenciostato/datos"
#define MQTT_SUB_TOPIC      "potenciostato/cmd"

// ----- PINES I2C (ADS1115) -----
#define I2C_MASTER_SDA_IO   8
#define I2C_MASTER_SCL_IO   9
#define I2C_MASTER_FREQ_HZ  400000  // Fast mode 400kHz
#define I2C_MASTER_NUM      0
#define I2C_TIMEOUT_MS      1000

// ----- PINES SPI (AD5662 DAC) -----
// AD5662 es solo escritura, no tiene MISO. GPIO2 queda libre para el MUX.
#define SPI_MISO_IO         -1
#define SPI_MOSI_IO         7
#define SPI_SCLK_IO         6
#define SPI_CS_IO           10
#define SPI_MAX_FREQ_HZ     1000000 // 1MHz

// ----- PINES MUX (MAX4558) -----
// Enable esta conectado a GND fisicamente (siempre habilitado)
#define MUX_SEL_A_IO        1
#define MUX_SEL_B_IO        2
#define MUX_SEL_C_IO        3

// ----- ADS1115 ADC -----
#define ADS1115_ADDR        0x48    // ADDR pin a GND
#define ADS1115_REG_CONV    0x00    // Registro de conversion (lectura)
#define ADS1115_REG_CONFIG  0x01    // Registro de configuracion
// Config: AIN0 single-ended, +/-4.096V (PGA=001), 128SPS, single-shot
// OS=1 | MUX=100 | PGA=001 | MODE=1 | DR=100 | COMP_QUE=11
// = 0b 1_100_001_1 _100_0_0_0_11 = 0xC383
#define ADS1115_CONFIG_DEFAULT  0xC383

// ----- AD5662 DAC (BIPOLAR via OPA703) -----
// VREF = 1.5V, salida DAC: 0 a 1.5V
// Con conversor bipolar OPA703:
//   DAC 0V     (code 0)     -> salida -4.5V
//   DAC 0.75V  (code 32768) -> salida  0V (punto medio)
//   DAC 1.5V   (code 65535) -> salida +4.5V
// Rango bipolar total: -4.5V a +4.5V
#define DAC_VREF            1.5f
#define DAC_BIPOLAR_MIN_V   (-4.5f)
#define DAC_BIPOLAR_MAX_V   (4.5f)
#define DAC_CODE_ZERO_V     32768   // Codigo DAC que produce 0V en salida bipolar
#define DAC_MAX_CODE        65535

// ----- MEDICION -----
#define MEASUREMENT_STEP_MS     10      // Intervalo del timer entre pasos de medicion
#define TRIANGULAR_STEP_MV      50      // Incremento en mV por paso de la onda triangular

#endif // APP_CONFIG_H
