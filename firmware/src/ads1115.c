#include "ads1115.h"
#include "board.h"
#include "hwregs.h"
#include "driver/gpio.h"
#include "esp_attr.h"

#define REG_CONV    0x00
#define REG_CONFIG  0x01
#define REG_LO      0x02
#define REG_HI      0x03

static i2c_master_dev_handle_t s_dev;
static TaskHandle_t s_task;
static uint32_t s_bit;

static void IRAM_ATTR rdy_isr(void *arg)
{
    (void)arg;
    BaseType_t woken = pdFALSE;
    xTaskNotifyFromISR(s_task, s_bit, eSetBits, &woken);
    portYIELD_FROM_ISR(woken);
}

static esp_err_t wr16(uint8_t reg, uint16_t v)
{
    uint8_t b[3] = { reg, (uint8_t)(v >> 8), (uint8_t)(v & 0xFF) };
    return i2c_master_transmit(s_dev, b, sizeof b, PS_I2C_TMO_MS);
}

esp_err_t ads_init(i2c_master_bus_handle_t bus, TaskHandle_t notify, uint32_t notify_bit)
{
    s_task = notify;
    s_bit = notify_bit;

    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PS_I2C_ADDR_ADS,
        .scl_speed_hz = PS_I2C_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_dev);
    if (err != ESP_OK) return err;

    /* Hi_thresh MSB=1 y Lo_thresh MSB=0 activan el modo conversión lista en ALERT/RDY */
    err = wr16(REG_LO, 0x0000);
    if (err == ESP_OK) err = wr16(REG_HI, 0x8000);
    if (err != ESP_OK) return err;

    const gpio_config_t rdy = {
        .pin_bit_mask = 1ULL << PS_PIN_ADS_RDY,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,   /* respaldo: la placa lleva pull-up externo */
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    err = gpio_config(&rdy);
    if (err != ESP_OK) return err;
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;   /* ya instalado: no es error */
    return gpio_isr_handler_add(PS_PIN_ADS_RDY, rdy_isr, NULL);
}

esp_err_t ads_start(uint8_t pga_idx, uint8_t dr_idx)
{
    return wr16(REG_CONFIG, ads_config_word(pga_idx, dr_idx));
}

esp_err_t ads_read(int16_t *raw)
{
    uint8_t reg = REG_CONV, b[2];
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, b, 2, PS_I2C_TMO_MS);
    if (err == ESP_OK) *raw = (int16_t)(((uint16_t)b[0] << 8) | b[1]);
    return err;
}
