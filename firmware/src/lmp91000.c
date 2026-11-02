#include "lmp91000.h"
#include "board.h"
#include "hwregs.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define REG_STATUS  0x00
#define REG_LOCK    0x01
#define REG_TIACN   0x10
#define REG_REFCN   0x11
#define REG_MODECN  0x12

static i2c_master_dev_handle_t s_dev;
static int s_refcn = -1;   /* último REFCN escrito; -1 = desconocido */

static esp_err_t wr(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = { reg, val };
    return i2c_master_transmit(s_dev, b, sizeof b, PS_I2C_TMO_MS);
}

esp_err_t lmp_init(i2c_master_bus_handle_t bus)
{
    const gpio_config_t menb = {
        .pin_bit_mask = 1ULL << PS_PIN_LMP_MENB,
        .mode = GPIO_MODE_OUTPUT,
    };
    esp_err_t err = gpio_config(&menb);
    if (err != ESP_OK) return err;
    err = gpio_set_level(PS_PIN_LMP_MENB, 0);
    if (err != ESP_OK) return err;

    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PS_I2C_ADDR_LMP,
        .scl_speed_hz = PS_I2C_HZ,
    };
    err = i2c_master_bus_add_device(bus, &cfg, &s_dev);
    if (err != ESP_OK) return err;
    s_refcn = -1;

    /* Espera de arranque (fuera de pasada): STATUS bit0 = 1 cuando el dispositivo está listo */
    for (int i = 0; i < 50; i++) {
        uint8_t reg = REG_STATUS, st = 0;
        if (i2c_master_transmit_receive(s_dev, &reg, 1, &st, 1, PS_I2C_TMO_MS) == ESP_OK && (st & 0x01))
            return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t lmp_configure(uint8_t gain_idx, uint8_t rload_idx)
{
    s_refcn = -1;
    esp_err_t err = wr(REG_LOCK, 0x00);   /* desbloquear TIACN/REFCN */
    if (err == ESP_OK) err = wr(REG_TIACN, lmp_regs_tiacn(gain_idx, rload_idx));
    if (err == ESP_OK) err = wr(REG_REFCN, lmp_regs_refcn(1, 0));
    if (err == ESP_OK) err = wr(REG_MODECN, LMP_MODECN_3LEAD);
    return err;
}

esp_err_t lmp_set_bias(int8_t sign, uint8_t bias_idx)
{
    uint8_t v = lmp_regs_refcn(sign, bias_idx);
    if (s_refcn == v) return ESP_OK;
    esp_err_t err = wr(REG_REFCN, v);
    if (err == ESP_OK) s_refcn = v;
    return err;
}

esp_err_t lmp_lock(void)
{
    return wr(REG_LOCK, 0x01);
}
