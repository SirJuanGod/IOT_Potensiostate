#include <math.h>
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "sdkconfig.h"
#include "board.h"
#include "ps_common.h"
#include "profile.h"
#include "protocol.h"
#include "store.h"
#include "lmp91000.h"
#include "ads1115.h"
#include "ad5662.h"
#include "engine.h"
#include "pub.h"
#include "net.h"

static const char *TAG = "main";

static void reply_err(ps_err_code_t code, const char *field, int32_t limit, const char *msg)
{
    ps_err_t e;
    ps_err_set(&e, code, field, limit, msg);
    pub_status("error", &e);
}

static bool cal_ok(const ps_cal_t *c)
{
    for (int i = 0; i < 8; i++)
        if (!isfinite(c->gain[i]) || !(c->gain[i] > 0.0f) || !isfinite(c->offset_na[i])) return false;
    return true;
}

/* Contexto de la tarea de esp-mqtt (única llamadora): estáticos para no cargar su pila. */
static void on_cmd(const char *data, int len)
{
    static ps_cmd_t cmd;
    static char buf[1536];
    ps_err_t err;
    if (!proto_parse_cmd(data, (size_t)len, &cmd, &err)) { pub_status("error", &err); return; }

    if (cmd.kind == PS_CMD_STOP) { engine_request_stop(); return; }
    if (cmd.kind == PS_CMD_RUN) {
        if (engine_submit(&cmd) != ESP_OK) reply_err(PS_ERR_BUSY, "cmd", 0, "medicion en curso");
        return;
    }
    /* Con un run en curso no se toca NVS: evita escrituras de flash (y lecturas que comparten estado con
     * meas_task en store_get_active_profile) durante una pasada. */
    if (engine_busy()) { reply_err(PS_ERR_BUSY, "cmd", 0, "medicion en curso"); return; }

    esp_err_t e = ESP_OK;
    int n;
    switch (cmd.kind) {
    case PS_CMD_SET_PROFILE:
        if (!profile_check(&cmd.profile, CONFIG_PS_DAC_VREF_MV, &err)) { pub_status("error", &err); return; }
        e = store_put_profile(&cmd.profile);
        if (e == ESP_ERR_NO_MEM) { reply_err(PS_ERR_RANGE, "profile", 4, "maximo 4 perfiles"); return; }
        break;
    case PS_CMD_SELECT_PROFILE:
        e = store_select_profile(cmd.name);
        if (e == ESP_ERR_NOT_FOUND) { reply_err(PS_ERR_RANGE, "name", 0, "perfil inexistente"); return; }
        break;
    case PS_CMD_SET_CAL:
        if (!cal_ok(&cmd.cal)) { reply_err(PS_ERR_RANGE, "cal", 0, "ganancia <= 0 o valor no finito"); return; }
        e = store_put_cal(&cmd.cal);
        break;
    case PS_CMD_GET_PROFILE:
    case PS_CMD_GET_CAL:
        if (cmd.kind == PS_CMD_GET_PROFILE) {
            store_get_active_profile(&cmd.profile);   /* en error devuelve el perfil por defecto */
            n = proto_write_profile(buf, sizeof buf, &cmd.profile);
        } else {
            store_get_cal(&cmd.cal);                  /* en error devuelve la calibración por defecto */
            n = proto_write_cal(buf, sizeof buf, &cmd.cal);
        }
        if (n < 0) reply_err(PS_ERR_HW, "buf", (int32_t)sizeof buf, "respuesta demasiado grande");
        else net_publish("status", buf, n, false);
        return;
    default:
        return;
    }
    if (e != ESP_OK) { reply_err(PS_ERR_HW, "nvs", 0, esp_err_to_name(e)); return; }
    pub_status("idle", NULL);
}

/* Un chip que no responde no impide arrancar la red: el equipo sigue accesible y los run informan el error HW. */
static void hw_check(const char *what, esp_err_t e)
{
    if (e != ESP_OK) ESP_LOGE(TAG, "%s: %s", what, esp_err_to_name(e));
}

void app_main(void)
{
    ESP_ERROR_CHECK(store_init());

    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PS_PIN_I2C_SDA,
        .scl_io_num = PS_PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,   /* respaldo: la placa lleva 4,7 kΩ externos */
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    /* meas_task antes que los drivers: ads_init necesita su handle para el aviso RDY */
    ESP_ERROR_CHECK(engine_start());
    hw_check("lmp91000", lmp_init(bus));
    hw_check("ads1115", ads_init(bus, engine_task(), EVT_ADC_RDY));
    hw_check("ad5662", ad5662_init());   /* inicializa también el bus SPI2 */
    /* tras el reset el LMP está bloqueado y en deep sleep (celda desconectada); el DAC queda en VREF de reposo */
    hw_check("estado seguro", engine_safe_state());

    ESP_ERROR_CHECK(pub_start());
    net_set_busy_query(engine_busy);
    ESP_ERROR_CHECK(net_start(on_cmd));
}
