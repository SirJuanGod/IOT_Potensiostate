# Firmware potenciostato (LMP91000 + AD5662 + ADS1115, ESP32-C3)

- Tests en el host: `pio test -e native`
- Build del dispositivo: `pio run -e esp32-c3-devkitm-1`

## Integración de `lib/`

Variante usada: **`lib/` plano de PlatformIO** (sin `CMakeLists.txt` por lib ni `EXTRA_COMPONENT_DIRS`); PlatformIO enlaza `lib/ps_common` en la build de ESP-IDF.

## Notas

- `platform = espressif32@6.13.0` fija ESP-IDF 5.5.3 (la versión sin fijar resolvía a IDF 6.1, donde `mqtt` ya no es componente del core).

- `sdkconfig.esp32-c3-devkitm-1` se genera desde `sdkconfig.defaults` y no se versiona.
