#include "pub.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "net.h"

#define BLOCK_BYTES 4096
#define RETRY_MS 500
#define GIVE_UP_MS 120000

typedef struct { ps_pass_ctx_t ctx; TaskHandle_t done; } job_t;

static const char *TAG = "pub";
static QueueHandle_t s_q;
static SemaphoreHandle_t s_status_mtx;
static char s_blk[BLOCK_BYTES];     /* único buffer de bloques; solo lo usa pub_task */
static char s_st[160];              /* buffer de pub_status, protegido por s_status_mtx */

/* Envía con reintentos cada 500 ms hasta el plazo (vTaskDelay permitido: esto no es la ruta de medida). */
static bool send_retry(const char *sub, int len, TickType_t deadline)
{
    while (net_publish(sub, s_blk, len, false) != ESP_OK) {
        if ((int32_t)(xTaskGetTickCount() - deadline) >= 0) return false;
        vTaskDelay(pdMS_TO_TICKS(RETRY_MS));
    }
    return true;
}

/* Un bloque que no cabe trunca la pasada: se avisa al cliente por status. Devuelve true (no es timeout). */
static bool fmt_fail(const char *what)
{
    ESP_LOGE(TAG, "%s no cabe en el bloque", what);
    ps_err_t err;
    ps_err_set(&err, PS_ERR_HW, "publish_format", 0, what);
    pub_status("error", &err);
    return true;
}

static bool send_pass(const ps_pass_ctx_t *c, TickType_t deadline)
{
    int len = proto_write_header(s_blk, sizeof s_blk, c->meta);
    if (len < 0) return fmt_fail("cabecera");   /* error de formato, no de red: no es timeout */
    if (!send_retry("data", len, deadline)) return false;

    size_t off = 0;
    uint32_t seq = 0;
    bool last = false;
    while (!last) {
        size_t used = 0;
        len = proto_write_rows(s_blk, sizeof s_blk, c->meta->id, c->meta->pass, seq++,
                               c->s + off, c->i_na + off, c->n - off, &used, &last);
        if (len < 0) return fmt_fail("fila");
        if (!send_retry("data", len, deadline)) return false;
        off += used;
    }

    if (c->send_result) {
        len = proto_write_result(s_blk, sizeof s_blk, c->meta->id, c->meta->params->tec, c->f, c->adj, c->n_adj, c->run_flags);
        if (len < 0) return fmt_fail("resultado");
        if (!send_retry("result", len, deadline)) return false;
    }
    return true;
}

static void pub_task(void *arg)
{
    (void)arg;
    job_t j;
    for (;;) {
        if (xQueueReceive(s_q, &j, portMAX_DELAY) != pdTRUE) continue;
        if (!send_pass(&j.ctx, xTaskGetTickCount() + pdMS_TO_TICKS(GIVE_UP_MS))) {
            ESP_LOGE(TAG, "publish_timeout: se descartan los datos de la pasada");
            ps_err_t err;
            ps_err_set(&err, PS_ERR_HW, "publish_timeout", 0, "sin conexión MQTT");
            pub_status("error", &err);
        }
        if (j.done) xTaskNotifyGiveIndexed(j.done, 1);
    }
}

esp_err_t pub_start(void)
{
    s_q = xQueueCreate(1, sizeof(job_t));
    s_status_mtx = xSemaphoreCreateMutex();
    if (!s_q || !s_status_mtx) return ESP_ERR_NO_MEM;
    return xTaskCreate(pub_task, "pub_task", 6144, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void pub_publish_pass(const ps_pass_ctx_t *ctx, TaskHandle_t done_task)
{
    job_t j = { .ctx = *ctx, .done = done_task };
    if (!s_q || xQueueSend(s_q, &j, 0) != pdTRUE) {      /* cola ocupada (pasada anterior sin terminar) o sin iniciar */
        ESP_LOGE(TAG, "no se pudo encolar la pasada");
        ps_err_t err;
        ps_err_set(&err, PS_ERR_BUSY, "publish_queue", 0, "pasada descartada");
        pub_status("error", &err);
        if (done_task) xTaskNotifyGiveIndexed(done_task, 1);
    }
}

void pub_status(const char *state, const ps_err_t *err)
{
    if (!s_status_mtx || xSemaphoreTake(s_status_mtx, portMAX_DELAY) != pdTRUE) return;
    int len = proto_write_status(s_st, sizeof s_st, state, err);
    if (len > 0) net_publish("status", s_st, len, strcmp(state, "idle") == 0 || strcmp(state, "offline") == 0);
    xSemaphoreGive(s_status_mtx);
}
