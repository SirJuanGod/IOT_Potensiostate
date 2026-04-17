/**
 * @file mux_driver.c
 * @brief Driver GPIO para MUX analógico MAX4558 — ESP-IDF.
 * Controla los pines de selección A, B, C para elegir entre 8 canales (0–7).
 * Ref: MAX4558 Datasheet Rev.19-4300, Analog Devices — Table 1 (Truth Table)
 * Ref: ESP-IDF GPIO API — gpio_set_level(), gpio_config()
 */

#include "mux_driver.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "app_config.h"

static const char *TAG = "MUX_DRIVER";

/* [FIX-3][FIX-4] Estado interno del driver */
static bool     s_initialized    = false;
static uint8_t  s_current_channel = MUX_CHANNEL_NONE;

/* Tabla de pines: índice 0=A, 1=B, 2=C */
static const gpio_num_t s_sel_pins[3] = {
    MUX_SEL_A_IO,
    MUX_SEL_B_IO,
    MUX_SEL_C_IO
};

/* ─────────────────────────────────────────────────────────────────────────── */

esp_err_t mux_driver_init(void)
{
    /* si ya está inicializado, no reconfigurar */
    if (s_initialized) {
        ESP_LOGW(TAG, "MUX ya inicializado (canal activo: %d), omitiendo re-init",
                 s_current_channel);
        return ESP_OK;
    }

    for (int i = 0; i < 3; i++) {
        gpio_config_t io_conf = {
            .pin_bit_mask  = (1ULL << s_sel_pins[i]),
            .mode          = GPIO_MODE_OUTPUT,
            .pull_up_en    = GPIO_PULLUP_DISABLE,
            .pull_down_en  = GPIO_PULLDOWN_DISABLE,
            .intr_type     = GPIO_INTR_DISABLE,
        };

        esp_err_t ret = gpio_config(&io_conf);
        if (ret != ESP_OK) {
            /*Log detallado con nombre del pin fallido */
            ESP_LOGE(TAG, "Fallo configurando GPIO%d (SEL_%c): %s",
                     s_sel_pins[i], 'A' + i, esp_err_to_name(ret));
            /* Rollback: poner en input los pines ya configurados */
            for (int j = 0; j < i; j++) {
                gpio_reset_pin(s_sel_pins[j]);
            }
            return ret;
        }

        /* Nivel inicial = 0 → canal 0 por defecto */
        ret = gpio_set_level(s_sel_pins[i], 0);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Fallo set_level GPIO%d: %s",
                     s_sel_pins[i], esp_err_to_name(ret));
            for (int j = 0; j <= i; j++) {
                gpio_reset_pin(s_sel_pins[j]);
            }
            return ret;
        }
    }

    s_initialized     = true;
    s_current_channel = 0;   /* Canal 0 activo tras init */

    ESP_LOGI(TAG, "MAX4558 listo. SEL_A=GPIO%d, SEL_B=GPIO%d, SEL_C=GPIO%d | Canal inicial: 0",
             MUX_SEL_A_IO, MUX_SEL_B_IO, MUX_SEL_C_IO);
    return ESP_OK;
}

/* ─────────────────────────────────────────────────────────────────────────── */

esp_err_t mux_select_channel(uint8_t channel)
{
    /* Inicialización */
    if (!s_initialized) {
        ESP_LOGE(TAG, "MUX no inicializado. Llamar mux_driver_init() primero");
        return ESP_ERR_INVALID_STATE;
    }

    if (channel > MUX_MAX_CHANNEL) {
        ESP_LOGE(TAG, "Canal inválido: %d (máximo: %d)", channel, MUX_MAX_CHANNEL);
        return ESP_ERR_INVALID_ARG;
    }

    if (channel == s_current_channel) {
        ESP_LOGD(TAG, "Canal %d ya activo, sin cambio", channel);
        return ESP_OK;
    }

    /* Decodificación binaria: A=bit0, B=bit1, C=bit2
     * Ref: MAX4558 Datasheet, Table 1 (Truth Table) */
    const uint32_t levels[3] = {
        (channel >> 0) & 0x01u,   /* SEL_A */
        (channel >> 1) & 0x01u,   /* SEL_B */
        (channel >> 2) & 0x01u,   /* SEL_C */
    };

    /*Verificar retorno de cada gpio_set_level() */
    for (int i = 0; i < 3; i++) {
        esp_err_t ret = gpio_set_level(s_sel_pins[i], levels[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Fallo set_level GPIO%d (SEL_%c) canal %d: %s",
                     s_sel_pins[i], 'A' + i, channel, esp_err_to_name(ret));
            return ret;
        }
    }

    ESP_LOGD(TAG, "Canal %d → A=%lu B=%lu C=%lu",
             channel, levels[0], levels[1], levels[2]);

    s_current_channel = channel;
    return ESP_OK;
}

/* ─────────────────────────────────────────────────────────────────────────── */

uint8_t mux_get_current_channel(void)
{
    return s_current_channel;   /* MUX_CHANNEL_NONE si no inicializado */
}

/* ─────────────────────────────────────────────────────────────────────────── */

esp_err_t mux_driver_deinit(void)
{
    /*Limpieza ordenada */
    if (!s_initialized) {
        return ESP_OK;
    }

    for (int i = 0; i < 3; i++) {
        gpio_reset_pin(s_sel_pins[i]);
    }

    s_initialized     = false;
    s_current_channel = MUX_CHANNEL_NONE;

    ESP_LOGI(TAG, "MUX liberado");
    return ESP_OK;
}