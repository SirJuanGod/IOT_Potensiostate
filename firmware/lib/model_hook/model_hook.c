#include "model_hook.h"

bool model_predict(const ps_features_t *f, const ps_params_t *p, float *conc)
{
    (void)f; (void)p; (void)conc;
    return false;   /* stub: el modelo llega en la etapa 2 */
}
