# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Firmware (C, ESP-IDF + FreeRTOS) for an ESP32-C3 SuperMini potentiostat that runs cyclic voltammetry: an AD5662 DAC (SPI) applies a triangular voltage sweep to an electrochemical cell, an ADS1115 ADC (I2C) reads the TIA output as current, and results are published as JSON over MQTT. Code comments, logs and the README are in Spanish.

## Two copies of the firmware — edit the right one

- `data_takes/Potensiostate/` — **the active project** (PlatformIO, `framework = espidf`). Modules live in `lib/<module>/` and `src/main.c` is the entry point. Its `README.md` is the main project documentation.
- `data_takes/version_1.0/` — older, plain ESP-IDF component layout (`components/*/` with `idf_component_register` + `REQUIRES`). Same modules, diverged code (e.g. it still defines `potenciostato_is_measuring()`, its config uses `WIFI_PASS` instead of `WIFI_PASSWORD`). Treat as legacy unless told otherwise; changes are not mirrored automatically.

## Commands (run from `data_takes/Potensiostate/`)

```bash
pio run -e esp32-c3-devkitm-1                  # build
pio run -e esp32-c3-devkitm-1 --target upload  # flash
pio device monitor                             # serial log
pio run -e esp32-c3-devkitm-1 -t menuconfig    # edit sdkconfig.esp32-c3-devkitm-1
```

There are no tests (`test/` only has the PlatformIO placeholder README) and no linter config. Wokwi simulation (`wokwi.toml`) points at `.pio/build/esp32-c3-devkitm-1/firmware.bin`, but `diagram.json` is still the template (ESP32-C3 + LCD1602) and does not model the real hardware. `sdkconfig` targets esp32c3, 2 MB flash, IDF 5.5.3. `.vscode/settings.json` hardcodes a Windows `IDF_PATH`.

## Architecture

Boot order in `src/main.c` matters: NVS → `potenciostato_init()` (I2C, SPI, MUX, task, timer; restart on failure) → `wifi_init_sta()` (blocks up to 10 s; on failure `app_main` returns and the device stays offline) → `mqtt_app_start(on_mqtt_command)`. Hardware is initialized before network so remote commands can't arrive against uninitialized drivers.

Measurement engine (`lib/core/potenciostato_core.c`):
- A periodic `esp_timer` (`MEASUREMENT_STEP_MS` = 10 ms) gives a binary semaphore; `meas_task` (prio 5, 4 KB stack) does all bus I/O. Never do SPI/I2C from the timer callback.
- 3-step state machine per point: `STEP_WRITE_DAC` → `STEP_START_ADC` (single-shot config write) → `STEP_READ_ADC`. So one point = 3 ticks = 30 ms; the 10 ms tick must stay > `ADC_CONVERSION_TIME_MS` (8 ms at 128 SPS).
- Sweep starts at −4500 mV, steps `TRIANGULAR_STEP_MV` up to +4500 mV, reverses, ends at −4500 mV (~361 points, buffer `MAX_MEAS_POINTS` = 400). Any bus error aborts the sweep.
- `finish_measurement()` serializes all points into a static 8 KB JSON buffer and publishes once to `potenciostato/datos` (QoS 1). `stop` only sets a flag; the task handles it on the next tick and still publishes partial data.

MQTT (`lib/mqtt_driver/mqtt_app.c`): subscribes to `potenciostato/cmd` on connect, reassembles fragmented payloads into a mutex-guarded 512 B buffer, and invokes the command callback with a local copy outside the mutex. `main.c` matches commands by prefix (`start`, `stop`).

Drivers are thin, idempotent-init wrappers over ESP-IDF with module-static state:
- `i2c_driver` uses the **legacy** `driver/i2c.h` API. Its functions are prefixed `i2c_master_*` (not `i2c_driver_*`) to avoid clashing with IDF symbols — don't rename back, and don't mix in the new `driver/i2c_master.h` driver (they can't coexist).
- `spi_driver` sends 24-bit AD5662 frames (`[PD1:PD0][D15..D0]`), SPI mode 1, no MISO. "ADVICE" in names means the AD5662 device.
- `mux_driver` drives MAX4558 select lines A/B/C as binary channel 0–7. It is initialized but the core never switches channels yet.

All pins, addresses, register values and sweep parameters live in `lib/config/app_config.h` (header-only, included by every module). Hardware notes encoded there: MUX SEL_C moved off GPIO2 (strapping pin) to GPIO10; DAC bipolar mapping code 0 → −4.5 V, 65535 → +4.5 V is nominal and needs calibration; I2C at 400 kHz needs external 4.7 kΩ pull-ups. `R_SHUNT_OHMS` is not in the config — the core falls back to 1000 Ω.

## Known gaps (verified in code)

- `potenciostato_is_measuring()` is declared in `core/potenciostato_core.h` but not defined in the PlatformIO copy.
- A full sweep's JSON (~28 B/point × ~361) exceeds the 8 KB buffer; the loop stops early and the output can end with a trailing comma (invalid JSON).
- README says ~5 V/s sweep rate; the real rate is 50 mV / 30 ms ≈ 1.67 V/s (as `app_config.h` states).
- WiFi credentials are placeholders in `app_config.h`; the default broker is the public `broker.hivemq.com` with no auth.
