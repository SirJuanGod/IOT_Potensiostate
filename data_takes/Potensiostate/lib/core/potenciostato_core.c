/**
 * @file potenciostato_core.c
 * @brief Núcleo del potenciostato — ESP32-C3, ESP-IDF v5.x
 *
 * Secuencia de medición (barrido triangular):
 *   STEP_WRITE_DAC  → AD5662 aplica voltaje via SPI
 *   STEP_START_ADC  → ADS1115 inicia conversión via I2C
 *   STEP_READ_ADC   → Lee resultado y avanza al siguiente punto
 *
 * Ref: AD5662 Datasheet Rev.C — Analog Devices
 * Ref: ADS1115 Datasheet SBAS444D — Texas Instruments
 */

#include "potenciostato_core.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "string.h"
#include "math.h"

#include "app_config.h"
#include "spi_driver.h"
#include "i2c_driver.h"
#include "mux_driver.h"
#include "mqtt_app.h"

static const char *TAG = "POTEN_CORE";

/* ── Tipos internos ─────────────────────────────────────────────────────── */

typedef enum {
    STEP_WRITE_DAC = 0,
    STEP_START_ADC,
    STEP_READ_ADC,
} meas_step_t;

typedef struct {
    float    voltage_mv;
    int16_t  adc_raw;
    float    current_ua;
} meas_point_t;

/* ── Estado interno ─────────────────────────────────────────────────────── */

#define MAX_MEAS_POINTS     400u
#define JSON_BUF_SIZE       8192u

static volatile bool      s_measuring       = false;
static volatile bool      s_stop_requested  = false;
static meas_step_t        s_step            = STEP_WRITE_DAC;

static meas_point_t       s_points[MAX_MEAS_POINTS];
static uint32_t           s_point_count     = 0;

static float              s_current_mv      = 0.0f;
static float              s_direction       = +1.0f;

static esp_timer_handle_t s_meas_timer      = NULL;
static TaskHandle_t       s_meas_task       = NULL;
static SemaphoreHandle_t  s_meas_sem        = NULL;

/* ── Conversión DAC ─────────────────────────────────────────────────────── */

/**
 * @brief Convierte voltaje en mV a código DAC de 16 bits (AD5662).
 *        Rango bipolar: -4500mV→code=0 | 0mV→code=32768 | +4500mV→code=65535
 *        Fórmula: code = (Vout - Vmin) / (Vmax - Vmin) * 65535
 *        Ref: AD5662 Datasheet Rev.C, Equation 1.
 */
static uint16_t voltage_mv_to_dac_code(float voltage_mv)
{
    const float vmin = DAC_BIPOLAR_MIN_V * 1000.0f;
    const float vmax = DAC_BIPOLAR_MAX_V * 1000.0f;

    if (voltage_mv <= vmin) return 0u;
    if (voltage_mv >= vmax) return DAC_MAX_CODE;

    float normalized = (voltage_mv - vmin) / (vmax - vmin);
    return (uint16_t)(normalized * (float)DAC_MAX_CODE + 0.5f);
}

/* ── Escritura DAC ──────────────────────────────────────────────────────── */

/**
 * @brief Envía trama de 24 bits al AD5662.
 *        Formato: [PD1=0][PD0=0][D15..D0] — operación normal (no power-down).
 *        [FIX SPI-2] Exactamente 3 bytes — AD5662 requiere trama de 24 bits.
 *        [FIX SPI-4] Buffer declarado DMA_ATTR para garantizar memoria DMA-capable.
 *        Ref: AD5662 Datasheet, Table 6 — Input Shift Register Format.
 */
static esp_err_t dac_write(uint16_t code)
{
    return spi_driver_write_dac(code, ADVICE_PD_NORMAL, 100);
}

/* ── Control ADS1115 ────────────────────────────────────────────────────── */

/**
 * @brief Inicia conversión single-shot en ADS1115.
 *        Escribe registro CONFIG con OS=1 (start single conversion).
 *        [FIX I2C] Renombrado: i2c_driver_write → i2c_master_write_bytes
 *        Ref: ADS1115 Datasheet SBAS444D, Section 9.6.3 — Config Register.
 */
static esp_err_t adc_start_conversion(void)
{
    uint8_t config_data[3];
    config_data[0] = ADS1115_REG_CONFIG;
    config_data[1] = (uint8_t)(ADS1115_CONFIG_DEFAULT >> 8);
    config_data[2] = (uint8_t)(ADS1115_CONFIG_DEFAULT & 0xFF);

    return i2c_master_write_bytes(ADS1115_ADDR, config_data, 3);
}

/**
 * @brief Lee registro de conversión del ADS1115 (16 bits, big-endian).
 *        [FIX I2C] Renombrado: i2c_driver_write_read → i2c_master_write_read
 *        Ref: ADS1115 Datasheet SBAS444D, Section 9.6.1 — Conversion Register.
 */
static esp_err_t adc_read(int16_t *raw_value)
{
    if (raw_value == NULL) return ESP_ERR_INVALID_ARG;

    uint8_t reg       = ADS1115_REG_CONV;
    uint8_t rx_buf[2] = {0, 0};

    esp_err_t ret = i2c_master_write_read(ADS1115_ADDR,
                                           &reg,   1,
                                           rx_buf, 2);
    if (ret != ESP_OK) return ret;

    /* ADS1115 big-endian: byte[0]=MSB, byte[1]=LSB */
    *raw_value = (int16_t)((rx_buf[0] << 8) | rx_buf[1]);
    return ESP_OK;
}

/**
 * @brief Convierte código ADC raw a corriente en µA.
 *        Vref ADS1115 = ±4.096V (PGA=001) → LSB = 0.125 mV/bit.
 *        Corriente = Vadc / R_SHUNT_OHMS
 *        Definir R_SHUNT_OHMS en app_config.h según el circuito TIA.
 *        Ref: ADS1115 Datasheet SBAS444D, Table 3 — Full-Scale Range.
 */
static float adc_raw_to_current_ua(int16_t raw)
{
    const float lsb_mv = 4096.0f / 32768.0f;   /* 0.125 mV/LSB */
    float voltage_mv   = (float)raw * lsb_mv;

#ifndef R_SHUNT_OHMS
  #define R_SHUNT_OHMS  1000.0f   /* 1kΩ por defecto — definir en app_config.h */
#endif

    return (voltage_mv / R_SHUNT_OHMS) * 1000.0f;   /* resultado en µA */
}

/* ── Finalización de medición ───────────────────────────────────────────── */

static void finish_measurement(void)
{
    esp_timer_stop(s_meas_timer);
    s_measuring = false;

    ESP_LOGI(TAG, "Medicion finalizada. %lu puntos recolectados",
             (unsigned long)s_point_count);

    /* Construir JSON y publicar por MQTT */
    static char json_buf[JSON_BUF_SIZE];
    int offset = 0;

    offset += snprintf(json_buf + offset, JSON_BUF_SIZE - offset,
                       "{\"puntos\":%lu,\"datos\":[",
                       (unsigned long)s_point_count);

    for (uint32_t i = 0; i < s_point_count && offset < (JSON_BUF_SIZE - 32); i++) {
        offset += snprintf(json_buf + offset, JSON_BUF_SIZE - offset,
                           "{\"v\":%.1f,\"i\":%.3f}%s",
                           s_points[i].voltage_mv,
                           s_points[i].current_ua,
                           (i < s_point_count - 1) ? "," : "");
    }
    offset += snprintf(json_buf + offset, JSON_BUF_SIZE - offset, "]}");

    esp_err_t ret = mqtt_publish_data(json_buf);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Fallo publicando MQTT: %s", esp_err_to_name(ret));
    }
}

/* ── Tarea de medición ──────────────────────────────────────────────────── */

static void meas_task_fn(void *pvParam)
{
    (void)pvParam;

    while (true) {
        /* Bloquearse esperando señal del timer — sin polling activo */
        if (xSemaphoreTake(s_meas_sem, portMAX_DELAY) != pdTRUE) continue;

        /* Verificar solicitud de parada */
        if (s_stop_requested) {
            finish_measurement();
            s_stop_requested = false;
            continue;
        }

        if (!s_measuring) continue;

        switch (s_step) {

        /* ── Paso 1: Escribir voltaje al DAC ── */
        case STEP_WRITE_DAC: {
            uint16_t code = voltage_mv_to_dac_code(s_current_mv);
            esp_err_t ret = dac_write(code);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Fallo DAC write: %s", esp_err_to_name(ret));
                finish_measurement();
                break;
            }
            s_step = STEP_START_ADC;
            break;
        }

        /* ── Paso 2: Iniciar conversión ADC ── */
        case STEP_START_ADC: {
            esp_err_t ret = adc_start_conversion();
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Fallo ADC start: %s", esp_err_to_name(ret));
                finish_measurement();
                break;
            }
            s_step = STEP_READ_ADC;
            break;
        }

        /* ── Paso 3: Leer ADC y avanzar voltaje ── */
        case STEP_READ_ADC: {
            int16_t raw = 0;
            esp_err_t ret = adc_read(&raw);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Fallo ADC read: %s", esp_err_to_name(ret));
                finish_measurement();
                break;
            }

            /* Guardar punto de medición */
            if (s_point_count < MAX_MEAS_POINTS) {
                s_points[s_point_count].voltage_mv = s_current_mv;
                s_points[s_point_count].adc_raw    = raw;
                s_points[s_point_count].current_ua = adc_raw_to_current_ua(raw);
                s_point_count++;
            }

            /* Avanzar voltaje en barrido triangular */
            s_current_mv += s_direction * (float)TRIANGULAR_STEP_MV;

            /* Invertir dirección en extremo positivo */
            if (s_current_mv >= DAC_BIPOLAR_MAX_V * 1000.0f) {
                s_current_mv = DAC_BIPOLAR_MAX_V * 1000.0f;
                s_direction  = -1.0f;
            }
            /* Barrido completo al llegar al extremo negativo */
            else if (s_current_mv <= DAC_BIPOLAR_MIN_V * 1000.0f) {
                s_current_mv = DAC_BIPOLAR_MIN_V * 1000.0f;
                finish_measurement();
                s_step = STEP_WRITE_DAC;
                break;
            }

            s_step = STEP_WRITE_DAC;
            break;
        }

        default:
            s_step = STEP_WRITE_DAC;
            break;
        }
    }

    vTaskDelete(NULL);
}

/* ── Callback del timer (contexto ISR) ─────────────────────────────────── */

static void IRAM_ATTR meas_timer_cb(void *arg)
{
    BaseType_t higher_priority_woken = pdFALSE;
    xSemaphoreGiveFromISR(s_meas_sem, &higher_priority_woken);
    portYIELD_FROM_ISR(higher_priority_woken);
}

/* ── API pública ────────────────────────────────────────────────────────── */

esp_err_t potenciostato_init(void)
{
    ESP_LOGI(TAG, "Inicializando potenciostato...");

    /* [FIX I2C] Renombrado: i2c_driver_init → i2c_master_init */
    esp_err_t ret = i2c_master_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo I2C: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = spi_driver_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo SPI: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = mux_driver_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo MUX: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Semáforo binario — sincronización timer → tarea */
    s_meas_sem = xSemaphoreCreateBinary();
    if (s_meas_sem == NULL) {
        ESP_LOGE(TAG, "Fallo creando semáforo");
        return ESP_ERR_NO_MEM;
    }

    /* Tarea de medición — prioridad 5, stack 4KB */
    BaseType_t task_ret = xTaskCreate(meas_task_fn, "meas_task",
                                       4096, NULL, 5, &s_meas_task);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Fallo creando meas_task");
        vSemaphoreDelete(s_meas_sem);
        s_meas_sem = NULL;
        return ESP_ERR_NO_MEM;
    }

    /* Timer periódico — no iniciar hasta recibir comando 'start' */
    esp_timer_create_args_t timer_args = {
        .callback        = meas_timer_cb,
        .arg             = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "meas_timer",
    };
    ret = esp_timer_create(&timer_args, &s_meas_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo creando timer: %s", esp_err_to_name(ret));
        vSemaphoreDelete(s_meas_sem);
        s_meas_sem = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "Potenciostato listo");
    return ESP_OK;
}

esp_err_t potenciostato_start_measurement(void)
{
    if (s_measuring) return ESP_ERR_INVALID_STATE;

    /* Resetear estado antes de arrancar el timer */
    s_stop_requested = false;
    s_point_count    = 0;
    s_step           = STEP_WRITE_DAC;
    s_current_mv     = DAC_BIPOLAR_MIN_V * 1000.0f;
    s_direction      = +1.0f;
    memset(s_points, 0, sizeof(s_points));

    esp_err_t ret = esp_timer_start_periodic(s_meas_timer,
                        (uint64_t)MEASUREMENT_STEP_MS * 1000ULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo iniciando timer: %s", esp_err_to_name(ret));
        return ret;
    }

    s_measuring = true;
    ESP_LOGI(TAG, "Medicion iniciada. Paso=%dmV, Intervalo=%dms",
             TRIANGULAR_STEP_MV, MEASUREMENT_STEP_MS);
    return ESP_OK;
}

void potenciostato_stop_measurement(void)
{
    if (!s_measuring) return;
    s_stop_requested = true;
    /* La tarea procesa el stop en el siguiente ciclo del semáforo */
}