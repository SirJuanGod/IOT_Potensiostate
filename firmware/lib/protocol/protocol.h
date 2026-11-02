/* Protocolo MQTT JSON: parseo de comandos y escritura de estado, datos y resultado.
 * Sin dependencias de ESP-IDF: el mismo código corre en el host y en el equipo. */
#pragma once
#include <stddef.h>
#include "ps_common.h"
#include "profile.h"
#include "analysis.h"
#include "autorange.h"

typedef enum { PS_CMD_RUN, PS_CMD_STOP, PS_CMD_SET_PROFILE, PS_CMD_GET_PROFILE, PS_CMD_SELECT_PROFILE, PS_CMD_SET_CAL, PS_CMD_GET_CAL } ps_cmd_kind_t;
typedef enum { PS_PASS_EXPLORE, PS_PASS_FINAL } ps_pass_t;

typedef struct {
    ps_cmd_kind_t kind;
    char id[32];
    bool auto_adj, gain_auto;
    uint8_t gain_idx;
    ps_params_t params;
    ps_profile_t profile;
    char name[16];
    ps_cal_t cal;
} ps_cmd_t;

typedef struct {
    const char *id; ps_pass_t pass; const ps_params_t *params; const char *profile_name;
    uint8_t gain_idx, rload_idx, pga_idx; uint32_t dac_vref_mv, n_samples; uint64_t t0_us; const char *fw_version;
} ps_run_meta_t;

#define PS_RUN_NO_PEAK 0x01
#define PS_RUN_PEAK_AT_EDGE 0x02
#define PS_RUN_ABORTED 0x04
#define PS_RUN_ERROR 0x08

/* Parsea un comando. Solo devuelve true si todo el mensaje es válido: un comando mal formado
 * nunca debe iniciar una medición. En error, err dice qué campo falló (nunca se trunca ni se recorta). */
bool proto_parse_cmd(const char *json, size_t len, ps_cmd_t *out, ps_err_t *err);

/* Los escritores devuelven la longitud escrita (sin NUL), o -1 si el mensaje no cabe. */
int proto_write_status(char *buf, size_t cap, const char *state, const ps_err_t *err /* NULL si no hay */);
int proto_write_header(char *buf, size_t cap, const ps_run_meta_t *m);
/* Escribe tantas filas como quepan en cap; consumed = filas escritas, last = ya no quedan filas.
 * -1 si ni una fila cabe. i_na[k] corresponde a s[k]. */
int proto_write_rows(char *buf, size_t cap, const char *id, ps_pass_t pass, uint32_t seq,
                     const ps_sample_t *s, const float *i_na, size_t n, size_t *consumed, bool *last);
int proto_write_result(char *buf, size_t cap, const char *id, ps_tec_t tec, const ps_features_t *f,
                       const ps_adj_t *adj, size_t n_adj, uint32_t run_flags);
int proto_write_profile(char *buf, size_t cap, const ps_profile_t *p);
int proto_write_cal(char *buf, size_t cap, const ps_cal_t *c);
