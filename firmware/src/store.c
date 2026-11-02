#include "store.h"
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "sdkconfig.h"

#define NS "ps"
#define VER 1
#define MAX_PROFILES 4

/* Un solo blob de tamaño fijo: si cambia el struct, el tamaño o la versión no coinciden y se trata como "no guardado". */
typedef struct { uint8_t ver, n, active, pad; ps_profile_t p[MAX_PROFILES]; } profiles_blob_t;
typedef struct { uint8_t ver, pad[3]; ps_cal_t cal; } cal_blob_t;

esp_err_t store_init(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        e = nvs_flash_init();
    }
    return e;
}

/* Lee un blob; ok=false si no existe, el tamaño no coincide o la versión es distinta (la versión es el primer byte). */
static esp_err_t blob_load(const char *key, void *buf, size_t size, bool *ok)
{
    *ok = false;
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READONLY, &h);
    if (e == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;      /* namespace aún no creado */
    if (e != ESP_OK) return e;
    size_t len = size;
    e = nvs_get_blob(h, key, buf, &len);
    nvs_close(h);
    if (e == ESP_ERR_NVS_NOT_FOUND || e == ESP_ERR_NVS_INVALID_LENGTH) return ESP_OK;
    if (e != ESP_OK) return e;
    *ok = (len == size && *(const uint8_t *)buf == VER);
    return ESP_OK;
}

static esp_err_t blob_save(const char *key, const void *buf, size_t size)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = nvs_set_blob(h, key, buf, size);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e;
}

static esp_err_t profiles_load(profiles_blob_t *b)
{
    bool ok;
    esp_err_t e = blob_load("profiles", b, sizeof *b, &ok);
    if (e != ESP_OK) return e;
    if (!ok || b->n > MAX_PROFILES || (b->n && b->active >= b->n)) {
        memset(b, 0, sizeof *b);
        b->ver = VER;
    }
    return ESP_OK;
}

static int find(const profiles_blob_t *b, const char *name)
{
    for (int i = 0; i < b->n; i++)
        if (strncmp(b->p[i].name, name, sizeof b->p[i].name) == 0) return i;
    return -1;
}

esp_err_t store_get_active_profile(ps_profile_t *out)
{
    static profiles_blob_t b;       /* estática: ~1 KB, evita la pila del llamador; no reentrante (solo la tarea de comandos) */
    ps_err_t err;
    esp_err_t e = profiles_load(&b);
    if (e == ESP_OK && b.n && profile_check(&b.p[b.active], CONFIG_PS_DAC_VREF_MV, &err)) {
        *out = b.p[b.active];
        return ESP_OK;
    }
    profile_default(out);
    return e;
}

esp_err_t store_put_profile(const ps_profile_t *p)
{
    static profiles_blob_t b;
    ps_err_t err;
    if (!profile_check(p, CONFIG_PS_DAC_VREF_MV, &err)) return ESP_ERR_INVALID_ARG;
    esp_err_t e = profiles_load(&b);
    if (e != ESP_OK) return e;
    int i = find(&b, p->name);
    if (i < 0) {
        if (b.n >= MAX_PROFILES) return ESP_ERR_NO_MEM;
        i = b.n++;
    }
    b.p[i] = *p;
    return blob_save("profiles", &b, sizeof b);
}

esp_err_t store_select_profile(const char *name)
{
    static profiles_blob_t b;
    esp_err_t e = profiles_load(&b);
    if (e != ESP_OK) return e;
    int i = find(&b, name);
    if (i < 0) return ESP_ERR_NOT_FOUND;
    b.active = (uint8_t)i;
    return blob_save("profiles", &b, sizeof b);
}

esp_err_t store_get_cal(ps_cal_t *out)
{
    cal_blob_t b;
    bool ok;
    esp_err_t e = blob_load("cal", &b, sizeof b, &ok);
    if (e == ESP_OK && ok) { *out = b.cal; return ESP_OK; }
    ps_cal_default(out);
    return e;
}

esp_err_t store_put_cal(const ps_cal_t *c)
{
    cal_blob_t b = { .ver = VER, .cal = *c };
    return blob_save("cal", &b, sizeof b);
}
