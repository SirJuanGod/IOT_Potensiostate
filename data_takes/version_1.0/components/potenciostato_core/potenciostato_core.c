#include "potenciostato_core.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "i2c_driver.h"
#include "spi_driver.h"
#include "mux_driver.h"
#include "mqtt_app.h"
#include "app_config.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "POTENCIOSTATO";

// ----- Estado del barrido triangular -----
#define MAX_MEASUREMENT_POINTS 400

static float s_voltage_buf[MAX_MEASUREMENT_POINTS];
static int16_t s_current_buf[MAX_MEASUREMENT_POINTS];

static float s_voltage;
static float s_direction;
static int s_point_count;
static bool s_measuring;
static esp_timer_handle_t s_meas_timer;

// Estado de la maquina para cada paso del timer:
// STEP_WRITE_DAC -> STEP_START_ADC -> STEP_WAIT_ADC -> STEP_READ_ADC
typedef enum {
    STEP_WRITE_DAC,
    STEP_START_ADC,
    STEP_READ_ADC,
} meas_step_t;

static meas_step_t s_step;

// ----- Funciones del hardware -----

esp_err_t dac_write(uint16_t code) {
    uint8_t tx_buf[3];
    tx_buf[0] = 0x00;
    tx_buf[1] = (uint8_t)(code >> 8);
    tx_buf[2] = (uint8_t)(code & 0xFF);
    return spi_driver_transfer_buffer(tx_buf, NULL, 3);
}

static esp_err_t adc_start_conversion(void) {
    uint8_t config_data[3];
    config_data[0] = ADS1115_REG_CONFIG;
    config_data[1] = (uint8_t)(ADS1115_CONFIG_DEFAULT >> 8);
    config_data[2] = (uint8_t)(ADS1115_CONFIG_DEFAULT & 0xFF);
    return i2c_driver_write(ADS1115_ADDR, config_data, 3);
}

esp_err_t adc_read(int16_t *raw_value) {
    if (raw_value == NULL) return ESP_ERR_INVALID_ARG;

    uint8_t reg = ADS1115_REG_CONV;
    esp_err_t ret = i2c_driver_write(ADS1115_ADDR, &reg, 1);
    if (ret != ESP_OK) return ret;

    uint8_t rx_buf[2];
    ret = i2c_driver_read(ADS1115_ADDR, rx_buf, 2);
    if (ret != ESP_OK) return ret;

    *raw_value = (int16_t)((rx_buf[0] << 8) | rx_buf[1]);
    return ESP_OK;
}

static uint16_t voltage_to_dac_code(float voltage_v) {
    if (voltage_v < DAC_BIPOLAR_MIN_V) voltage_v = DAC_BIPOLAR_MIN_V;
    if (voltage_v > DAC_BIPOLAR_MAX_V) voltage_v = DAC_BIPOLAR_MAX_V;
    float normalized = (voltage_v - DAC_BIPOLAR_MIN_V) / (DAC_BIPOLAR_MAX_V - DAC_BIPOLAR_MIN_V);
    uint32_t code = (uint32_t)(normalized * DAC_MAX_CODE);
    if (code > DAC_MAX_CODE) code = DAC_MAX_CODE;
    return (uint16_t)code;
}

// ----- Publicar resultados al finalizar -----

static void publish_results(void) {
    char json_buf[128];
    snprintf(json_buf, sizeof(json_buf),
             "{\"status\":\"done\",\"points\":%d,\"v_start\":%.1f,\"v_end\":%.1f,\"i_start\":%d,\"i_end\":%d}",
             s_point_count,
             s_voltage_buf[0], s_voltage_buf[s_point_count - 1],
             s_current_buf[0], s_current_buf[s_point_count - 1]);
    mqtt_publish_data(json_buf);
}

// ----- Finalizar barrido -----

static void finish_measurement(void) {
    esp_timer_stop(s_meas_timer);
    s_measuring = false;

    // DAC al punto medio (0V bipolar)
    dac_write(DAC_CODE_ZERO_V);

    ESP_LOGI(TAG, "Barrido completado: %d puntos", s_point_count);

    if (s_point_count > 0) {
        publish_results();
    }
}

// ----- Callback del timer (se ejecuta cada MEASUREMENT_STEP_MS ms) -----

static void measurement_timer_cb(void *arg) {
    esp_err_t ret;

    switch (s_step) {
    case STEP_WRITE_DAC: {
        uint16_t code = voltage_to_dac_code(s_voltage);
        ret = dac_write(code);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error DAC en paso %d", s_point_count);
            finish_measurement();
            return;
        }
        s_step = STEP_START_ADC;
        break;
    }

    case STEP_START_ADC:
        ret = adc_start_conversion();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error iniciando ADC en paso %d", s_point_count);
            finish_measurement();
            return;
        }
        // El proximo tick del timer leera el resultado (~10ms despues)
        s_step = STEP_READ_ADC;
        break;

    case STEP_READ_ADC: {
        int16_t adc_raw = 0;
        ret = adc_read(&adc_raw);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error leyendo ADC en paso %d", s_point_count);
            finish_measurement();
            return;
        }

        // Guardar datos
        if (s_point_count < MAX_MEASUREMENT_POINTS) {
            s_voltage_buf[s_point_count] = s_voltage * 1000.0f;
            s_current_buf[s_point_count] = adc_raw;
            s_point_count++;
        }

        // Avanzar voltaje
        float step_v = TRIANGULAR_STEP_MV / 1000.0f;
        s_voltage += s_direction * step_v;

        if (s_voltage >= DAC_BIPOLAR_MAX_V) {
            s_voltage = DAC_BIPOLAR_MAX_V;
            s_direction = -1.0f;
        } else if (s_voltage <= DAC_BIPOLAR_MIN_V && s_direction < 0) {
            finish_measurement();
            return;
        }

        if (s_point_count >= MAX_MEASUREMENT_POINTS) {
            finish_measurement();
            return;
        }

        s_step = STEP_WRITE_DAC;
        break;
    }
    }
}

// ----- API publica -----

esp_err_t potenciostato_init(void) {
    ESP_LOGI(TAG, "Inicializando sistema potenciostato...");

    esp_err_t ret = i2c_driver_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al iniciar I2C");
        return ret;
    }

    ret = spi_driver_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al iniciar SPI");
        return ret;
    }

    ret = mux_driver_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al iniciar MUX");
        return ret;
    }

    ret = dac_write(DAC_CODE_ZERO_V);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al configurar DAC al punto medio");
        return ret;
    }

    // Crear timer periodico para los pasos de medicion
    const esp_timer_create_args_t timer_args = {
        .callback = measurement_timer_cb,
        .name = "meas_timer",
    };
    ret = esp_timer_create(&timer_args, &s_meas_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al crear timer de medicion");
        return ret;
    }

    s_measuring = false;
    ESP_LOGI(TAG, "Potenciostato inicializado correctamente");
    return ESP_OK;
}

esp_err_t potenciostato_start_measurement(void) {
    if (s_measuring) {
        ESP_LOGW(TAG, "Ya hay una medicion en curso");
        return ESP_ERR_INVALID_STATE;
    }

    s_voltage = DAC_BIPOLAR_MIN_V;
    s_direction = 1.0f;
    s_point_count = 0;
    s_step = STEP_WRITE_DAC;
    s_measuring = true;

    ESP_LOGI(TAG, "Iniciando barrido triangular de %.1fV a %.1fV",
             DAC_BIPOLAR_MIN_V, DAC_BIPOLAR_MAX_V);

    esp_err_t ret = esp_timer_start_periodic(s_meas_timer, MEASUREMENT_STEP_MS * 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error arrancando timer");
        s_measuring = false;
        return ret;
    }

    return ESP_OK;
}

void potenciostato_stop_measurement(void) {
    if (s_measuring) {
        esp_timer_stop(s_meas_timer);
        s_measuring = false;
        dac_write(DAC_CODE_ZERO_V);
        ESP_LOGI(TAG, "Medicion detenida manualmente");
    }
}

bool potenciostato_is_measuring(void) {
    return s_measuring;
}
