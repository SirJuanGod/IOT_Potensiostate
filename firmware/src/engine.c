#include "engine.h"
#include <stdint.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "ps_common.h"
#include "potential.h"
#include "technique.h"
#include "profile.h"
#include "analysis.h"
#include "autorange.h"
#include "model_hook.h"
#include "lmp91000.h"
#include "ad5662.h"
#include "ads1115.h"
#include "store.h"
#include "pub.h"

#define FW_VERSION "0.1.0"
#define VREF_MV CONFIG_PS_DAC_VREF_MV
#define EVT_ALL (EVT_RUN | EVT_STEP_END | EVT_ADC_TRIGGER | EVT_ADC_RDY | EVT_STOP)
#define EVT_STEP_BITS (EVT_STEP_END | EVT_ADC_TRIGGER | EVT_ADC_RDY)
#define MAX_ADJ 8

typedef enum { PASS_OK, PASS_ABORTED, PASS_ERROR } pass_res_t;

static const char *TAG = "engine";
static TaskHandle_t s_task;
static esp_timer_handle_t s_step_tmr, s_adc_tmr;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_busy;
static ps_cmd_t s_cmd;            /* run en curso: solo se escribe con s_busy recién tomado */

/* Estado de la pasada. pub_task lo lee hasta el aviso de índice 1; solo entonces se reutiliza. */
static ps_sample_t s_samples[PS_MAX_SAMPLES];
static float s_i_na[PS_MAX_SAMPLES];
static int16_t s_x[PS_MAX_SAMPLES];
static float s_y[PS_MAX_SAMPLES];
static size_t s_n;
static ps_profile_t s_prof;
static ps_cal_t s_cal;
static ps_params_t s_explore, s_final;
static ps_features_t s_feat;
static ps_adj_t s_adj[MAX_ADJ];
static ps_run_meta_t s_meta;
static ps_err_t s_err;            /* motivo del último PASS_ERROR */

/* Callback de esp_timer (tarea esp_timer, prioridad mayor que meas_task): arg = bit de evento. */
static void tmr_cb(void *arg)
{
    xTaskNotifyIndexed(s_task, 0, (uint32_t)(uintptr_t)arg, eSetBits);
}

static pass_res_t fail(ps_err_code_t code, const char *field, int32_t limit, const char *msg)
{
    ESP_LOGE(TAG, "%s: %s", field, msg);
    ps_err_set(&s_err, code, field, limit, msg);
    return PASS_ERROR;
}

static pass_res_t fail_hw(const char *field, esp_err_t e)
{
    return fail(PS_ERR_HW, field, 0, esp_err_to_name(e));
}

/* Arma t para el instante absoluto target_us. Si ya pasó, no se arma: el bit queda en *pending para atenderlo ya. */
static esp_err_t arm_at(esp_timer_handle_t t, int64_t target_us, uint32_t bit, uint32_t *pending)
{
    int64_t now = esp_timer_get_time();
    if (target_us <= now) { *pending |= bit; return ESP_OK; }
    esp_timer_stop(t);   /* normalmente ya disparó; INVALID_STATE se ignora */
    return esp_timer_start_once(t, (uint64_t)(target_us - now));
}

static esp_err_t safe_state(const char **field)
{
    ps_bias_t b;
    potential_solve(0, VREF_MV, &b);   /* E = 0: bias 0 % y VREF = PS_VREF_ZERO_MV */
    esp_err_t el = lmp_set_bias(+1, 0);
    esp_err_t ed = ad5662_write(b.dac_code);
    esp_err_t ek = lmp_lock();
    if (el == ESP_OK) el = ek;
    *field = el != ESP_OK ? "lmp" : "dac";
    return el != ESP_OK ? el : ed;
}

esp_err_t engine_safe_state(void)
{
    const char *field;
    return safe_state(&field);
}

/* Una pasada completa en s_samples. Entre pasos solo se bloquea en la notificación: sin vTaskDelay ni esperas activas. */
static pass_res_t run_pass(const ps_params_t *p, uint8_t gain, uint8_t pga, int64_t *t0_out)
{
    uint32_t ev = 0;
    esp_err_t e;
    s_n = 0;
    *t0_out = esp_timer_get_time();
    /* stop ya recibido (junto con RUN o entre pasadas): no se toca la celda. Se lee el valor y no el estado,
     * porque la espera de meas_task pudo consumir el estado dejando EVT_STOP en el valor. */
    if (ulTaskNotifyValueClearIndexed(NULL, 0, EVT_STOP) & EVT_STOP) return PASS_ABORTED;

    if ((e = lmp_configure(gain, s_prof.rload_idx)) != ESP_OK) return fail_hw("lmp", e);
    /* asentamiento tras configurar el LMP, antes del primer paso; +1 tick porque vTaskDelay(n) puede durar n-1 */
    vTaskDelay(pdMS_TO_TICKS((s_prof.settle_us + 999) / 1000) + 1);

    const int64_t t0 = esp_timer_get_time();
    *t0_out = t0;
    int64_t t_end = t0;
    ps_bias_t prev;
    potential_solve(0, VREF_MV, &prev);   /* lmp_configure deja bias 0 % y el DAC sigue en el estado seguro */
    const uint32_t n_steps = tech_num_steps(p);
    for (uint32_t k = 0; k < n_steps; k++) {
        ps_step_t st;
        ps_bias_t b;
        tech_step_at(p, k, &st);
        /* nada queda en vuelo del paso anterior: sus dos timers ya dispararon y su RDY se consumió */
        ulTaskNotifyValueClearIndexed(NULL, 0, EVT_STEP_BITS);
        if (!potential_solve(st.e_mv, VREF_MV, &b)) return fail(PS_ERR_RANGE, "E", st.e_mv, "potencial fuera de rango");
        /* el orden deja entre las dos escrituras el menor de p_viejo·V_nuevo y p_nuevo·V_viejo */
        const bool dac_first = potential_dac_first(&prev, &b);
        if (dac_first && (e = ad5662_write(b.dac_code)) != ESP_OK) return fail_hw("dac", e);
        if ((e = lmp_set_bias(b.sign, b.bias_idx)) != ESP_OK) return fail_hw("lmp", e);
        if (!dac_first && (e = ad5662_write(b.dac_code)) != ESP_OK) return fail_hw("dac", e);
        prev = b;

        t_end += st.dur_us;   /* tiempo absoluto: los retrasos de un paso no se acumulan */
        ev = 0;
        int dr = -1;
        if (st.sample) {
            if (s_n >= PS_MAX_SAMPLES) return fail(PS_ERR_RANGE, "points", PS_MAX_SAMPLES, "demasiados puntos");
            /* el disparo va en t_end − conv − margen: el margen también sale de la ventana (como ps_min_sample_step_us) */
            const uint32_t pre = s_prof.settle_us + PS_WRITE_US + PS_ADC_MARGIN_US;
            dr = st.dur_us > pre ? ps_pick_dr(st.dur_us - pre) : -1;
            if (dr < 0) return fail(PS_ERR_RANGE, "dur_us", (int32_t)st.dur_us, "paso mas corto que la conversion");
            /* la conversión termina al final del paso, como en KickStat */
            e = arm_at(s_adc_tmr, t_end - ps_adc_conv_us((uint8_t)dr) - PS_ADC_MARGIN_US, EVT_ADC_TRIGGER, &ev);
            if (e != ESP_OK) return fail_hw("timer", e);
        }
        if ((e = arm_at(s_step_tmr, t_end, EVT_STEP_END, &ev)) != ESP_OK) return fail_hw("timer", e);

        bool stepped = false, started = false, got = !st.sample;
        int64_t rdy_deadline = 0;
        for (;;) {
            if (ev & EVT_STOP) return PASS_ABORTED;
            /* RDY antes que TRIGGER: un RDY del mismo lote que el disparo no puede ser de esta conversión */
            if ((ev & EVT_ADC_RDY) && started && !got) {
                const int64_t t_rdy = esp_timer_get_time();
                int16_t raw;
                if ((e = ads_read(&raw)) != ESP_OK) return fail_hw("adc", e);
                s_samples[s_n++] = (ps_sample_t){
                    .t_us = (uint32_t)(t_rdy - t0), .x_mv = st.x_mv, .e_set_mv = st.e_mv, .e_apl_uv = b.e_apl_uv,
                    .sign = b.sign, .bias_idx = b.bias_idx, .dac_code = b.dac_code, .adc_raw = raw,
                    .pga = pga, .dr = (uint8_t)dr, .tag = st.tag, .cycle = st.cycle, .branch = st.branch,
                    .flags = b.snapped ? PS_FLAG_SNAPPED : 0,
                };
                got = true;
            }
            if ((ev & EVT_ADC_TRIGGER) && st.sample && !started) {
                if ((e = ads_start(pga, (uint8_t)dr)) != ESP_OK) return fail_hw("adc", e);
                started = true;
            }
            if ((ev & EVT_STEP_END) && !stepped) {
                stepped = true;
                if (!got) {
                    /* defensivo: el disparo vence antes que el fin de paso, pero nunca se espera sin conversión */
                    if (!started) {
                        if ((e = ads_start(pga, (uint8_t)dr)) != ESP_OK) return fail_hw("adc", e);
                        started = true;
                    }
                    rdy_deadline = esp_timer_get_time() + 2 * (int64_t)ps_adc_conv_us((uint8_t)dr);
                }
            }
            if (stepped && got) break;

            TickType_t wait = portMAX_DELAY;
            if (stepped) {   /* muestra pendiente tras el fin de paso: RDY con plazo de 2·conv */
                const int64_t left = rdy_deadline - esp_timer_get_time();
                if (left <= 0) return fail_hw("adc", ESP_ERR_TIMEOUT);
                wait = pdMS_TO_TICKS((uint32_t)((left + 999) / 1000)) + 1;
            }
            ev = 0;
            xTaskNotifyWaitIndexed(0, 0, EVT_ALL, &ev, wait);   /* en timeout ev = 0 y el plazo se revisa arriba */
        }
    }
    return PASS_OK;
}

/* Corrientes con la calibración del índice de ganancia, flag de recorte del TIA y características de la pasada. */
static void analyze(ps_tec_t tec, uint8_t gain)
{
    const uint32_t r_tia = ps_r_tia_ohm(gain, s_prof.r_ext_ohm);
    for (size_t k = 0; k < s_n; k++) {
        ps_sample_t *m = &s_samples[k];
        s_i_na[k] = ps_current_na(m->adc_raw, m->pga, r_tia, s_cal.offset_na[gain], s_cal.gain[gain]);
        if (ps_tia_frac(m->adc_raw, m->pga, m->dac_code, VREF_MV) > 1.0f) m->flags |= PS_FLAG_TIA_CLIP;
    }
    analysis_features(s_samples, s_i_na, s_n, tec, &s_prof, s_x, s_y, &s_feat);
}

/* "running" → pasada → estado seguro (siempre) → análisis. */
static pass_res_t do_pass(const ps_params_t *p, ps_gain_knobs_t k, int64_t *t0)
{
    pub_status("running", NULL);
    pass_res_t r = run_pass(p, k.gain_idx, k.pga_idx, t0);
    esp_timer_stop(s_adc_tmr);   /* tras abortar o fallar pueden seguir armados */
    esp_timer_stop(s_step_tmr);
    const char *field;
    esp_err_t e = safe_state(&field);
    if (e != ESP_OK) e = safe_state(&field);   /* un reintento */
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "estado seguro fallido (%s: %s): la celda puede seguir polarizada", field, esp_err_to_name(e));
        if (r != PASS_ERROR) r = fail_hw(field, e);
    }
    analyze(p->tec, k.gain_idx);
    return r;
}

/* Encola la pasada en pub_task y espera el aviso de índice 1: hasta entonces los buffers no se tocan. */
static void publish(ps_pass_t pass, const ps_params_t *p, ps_gain_knobs_t k, int64_t t0, size_t n_adj,
                    uint32_t flags, bool send_result)
{
    s_meta = (ps_run_meta_t){
        .id = s_cmd.id, .pass = pass, .params = p, .profile_name = s_prof.name, .gain_idx = k.gain_idx,
        .rload_idx = s_prof.rload_idx, .pga_idx = k.pga_idx, .dac_vref_mv = VREF_MV, .n_samples = (uint32_t)s_n,
        .t0_us = (uint64_t)t0, .fw_version = FW_VERSION, .r_ext_ohm = s_prof.r_ext_ohm,
        .cal_offset_na = s_cal.offset_na[k.gain_idx], .cal_gain = s_cal.gain[k.gain_idx],
    };
    const ps_pass_ctx_t ctx = {
        .meta = &s_meta, .s = s_samples, .i_na = s_i_na, .n = s_n, .f = &s_feat, .adj = s_adj, .n_adj = n_adj,
        .run_flags = flags, .send_result = send_result,
    };
    pub_publish_pass(&ctx, s_task);
    ulTaskNotifyTakeIndexed(1, pdTRUE, portMAX_DELAY);
}

/* Última publicación del run, con resultado. */
static void finish(pass_res_t r, ps_pass_t pass, const ps_params_t *p, ps_gain_knobs_t k, int64_t t0,
                   size_t n_adj, uint32_t flags)
{
    float conc;
    if (r == PASS_ABORTED) flags |= PS_RUN_ABORTED;
    if (r == PASS_ERROR) { flags |= PS_RUN_ERROR; pub_status("error", &s_err); }
    if (r == PASS_OK && p->tec != PS_TEC_CA && !s_feat.has_peak) flags |= PS_RUN_NO_PEAK;
    if (r == PASS_OK && model_predict(&s_feat, p, &conc)) ESP_LOGI(TAG, "concentracion estimada: %.4f", conc);
    publish(pass, p, k, t0, n_adj, flags, true);
}

/* Devuelve true si llegó a tocar la celda (entonces se publica "idle" al terminar). */
static bool run(void)
{
    ps_err_t err;
    int64_t t0;
    store_get_active_profile(&s_prof);   /* en error deja profile_default */
    store_get_cal(&s_cal);               /* en error deja ps_cal_default */
    const uint8_t gain = s_cmd.gain_auto ? s_prof.default_gain_idx : s_cmd.gain_idx;
    if (!profile_validate_run(&s_prof, &s_cmd.params, gain, VREF_MV, &err)) {
        pub_status("error", &err);
        return false;
    }

    ps_gain_knobs_t k = { .gain_idx = gain, .pga_idx = PS_PGA_DEFAULT };
    const ps_params_t *fp = &s_cmd.params;
    size_t n_adj = 0;
    uint32_t flags = 0;
    if (s_cmd.auto_adj) {
        autorange_explore_params(&s_cmd.params, &s_prof, &s_explore);
        if (!profile_validate_run(&s_prof, &s_explore, k.gain_idx, VREF_MV, &err)) {   /* no debería pasar */
            ESP_LOGW(TAG, "exploracion invalida (%s): se exploran los parametros pedidos", err.field);
            s_explore = s_cmd.params;
        }
        pass_res_t r = do_pass(&s_explore, k, &t0);
        if (r != PASS_OK) { finish(r, PS_PASS_EXPLORE, &s_explore, k, t0, 0, 0); return true; }
        if (s_explore.tec != PS_TEC_CA && !s_feat.has_peak) flags |= PS_RUN_NO_PEAK;   /* spec §10 */

        autorange_input_t in = {
            .has_peak = s_feat.has_peak, .ep_mv = s_feat.pk.ep_mv, .fwhm_mv = s_feat.pk.fwhm_mv, .at_edge = s_feat.pk.at_edge,
            .peak_fs_frac = ps_na_to_fs_frac(s_feat.pk.ip_na, k.pga_idx, ps_r_tia_ohm(k.gain_idx, s_prof.r_ext_ohm)),
        };
        for (size_t i = 0; i < s_n; i++) {
            const ps_sample_t *m = &s_samples[i];
            uint16_t a = (uint16_t)abs(m->adc_raw);
            float t = ps_tia_frac(m->adc_raw, m->pga, m->dac_code, VREF_MV);
            if (a > in.max_abs_raw) in.max_abs_raw = a;
            if (t > in.tia_frac) in.tia_frac = t;
        }
        publish(PS_PASS_EXPLORE, &s_explore, k, t0, 0, 0, false);

        ps_gain_knobs_t fk;
        bool at_edge = false;
        n_adj = autorange_decide(&in, &s_prof, &s_cmd.params, &k, &s_final, &fk, s_adj, MAX_ADJ, &at_edge);
        if (profile_validate_run(&s_prof, &s_final, fk.gain_idx, VREF_MV, &err)) {
            fp = &s_final;
            k = fk;
            if (at_edge) flags |= PS_RUN_PEAK_AT_EDGE;
        } else {   /* no debería pasar: se mide lo pedido y se marca el error del ajuste */
            ESP_LOGW(TAG, "ajuste invalido (%s): se usan los parametros pedidos", err.field);
            flags |= PS_RUN_ERROR;
            n_adj = 0;
        }
    }
    pass_res_t r = do_pass(fp, k, &t0);
    finish(r, PS_PASS_FINAL, fp, k, t0, n_adj, flags);
    return true;
}

static void meas_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t ev = 0;
        /* EVT_STOP no se borra aquí: si llega en el mismo lote que RUN lo atiende run_pass;
         * un stop viejo lo borra engine_submit antes de avisar RUN. */
        xTaskNotifyWaitIndexed(0, 0, EVT_RUN | EVT_STEP_BITS, &ev, portMAX_DELAY);
        if (!(ev & EVT_RUN)) continue;   /* stop sin run en curso o restos de una pasada */
        ESP_LOGI(TAG, "run '%s'", s_cmd.id);
        bool touched = run();
        s_busy = false;   /* s_cmd y los buffers ya están libres (la publicación terminó) */
        if (touched) pub_status("idle", NULL);
    }
}

esp_err_t engine_start(void)
{
    const esp_timer_create_args_t step = { .callback = tmr_cb, .arg = (void *)(uintptr_t)EVT_STEP_END, .name = "ps_step" };
    const esp_timer_create_args_t adc = { .callback = tmr_cb, .arg = (void *)(uintptr_t)EVT_ADC_TRIGGER, .name = "ps_adc" };
    esp_err_t e = esp_timer_create(&step, &s_step_tmr);
    if (e == ESP_OK) e = esp_timer_create(&adc, &s_adc_tmr);
    if (e != ESP_OK) return e;
    return xTaskCreate(meas_task, "meas_task", 6144, NULL, 10, &s_task) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

TaskHandle_t engine_task(void)
{
    return s_task;
}

esp_err_t engine_submit(const ps_cmd_t *run)
{
    if (!s_task || !run) return ESP_ERR_INVALID_ARG;
    taskENTER_CRITICAL(&s_mux);
    const bool was = s_busy;
    s_busy = true;
    taskEXIT_CRITICAL(&s_mux);
    if (was) return ESP_ERR_INVALID_STATE;
    s_cmd = *run;
    ulTaskNotifyValueClearIndexed(s_task, 0, EVT_STOP);   /* stop anterior a este run: no lo aborta */
    xTaskNotifyIndexed(s_task, 0, EVT_RUN, eSetBits);
    return ESP_OK;
}

void engine_request_stop(void)
{
    if (s_task) xTaskNotifyIndexed(s_task, 0, EVT_STOP, eSetBits);
}

bool engine_busy(void)
{
    return s_busy;
}
