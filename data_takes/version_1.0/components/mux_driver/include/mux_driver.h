#ifndef MUX_DRIVER_H
#define MUX_DRIVER_H

#include "esp_err.h"
#include <stdint.h>

/**
 * @brief Inicializa los GPIO de seleccion del MAX4558 (A, B, C)
 * El pin ENABLE esta conectado a GND fisicamente (siempre habilitado).
 */
esp_err_t mux_driver_init(void);

/**
 * @brief Selecciona un canal del MAX4558 (0-7)
 * @param channel Canal a seleccionar (0 a 7)
 *   canal 0: A=0 B=0 C=0
 *   canal 1: A=1 B=0 C=0
 *   canal 2: A=0 B=1 C=0
 *   canal 3: A=1 B=1 C=0
 *   canal 4: A=0 B=0 C=1
 *   canal 5: A=1 B=0 C=1
 *   canal 6: A=0 B=1 C=1
 *   canal 7: A=1 B=1 C=1
 */
esp_err_t mux_select_channel(uint8_t channel);

#endif // MUX_DRIVER_H
