# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Firmware (C, ESP-IDF 5.5.3 via PlatformIO + FreeRTOS) for an ESP32-C3 potentiostat: an AD5662 DAC (SPI) drives the external VREF of an LMP91000 AFE (I2C 0x48), which sets the cell potential and converts current with its TIA; an ADS1115 (I2C 0x49, ALERT/RDY interrupt) reads `VOUT − C1` differentially through an op-amp buffer on C1. Techniques: CV, SWV, DPV, NPV, CA. Results are published as JSON over MQTT. Code comments, logs and docs are in Spanish.

## Layout — edit the right project

- `firmware/` — **the active project**. Its `README.md` documents commands, MQTT protocol, wiring and the hardware bring-up checklist. Design spec: `docs/superpowers/specs/2026-11-02-potenciostato-lmp91000-design.md`.
- `data_takes/` — **legacy** (old ±4.5 V bipolar DAC design, `data_takes/Potensiostate/` and `data_takes/version_1.0/`). Do not touch unless explicitly asked.

## Commands (run from `firmware/`)

```bash
pio test -e native                  # all host tests (Unity, -std=c11 -Wall -Wextra -Werror)
pio test -e native -f test_<x>      # one module, e.g. test_protocol
pio run -e esp32-c3-devkitm-1       # device build (first run downloads the toolchain; allow ~10 min)
pio run -e esp32-c3-devkitm-1 -t upload && pio device monitor
```

WiFi, broker URI, device id and `PS_DAC_VREF_MV` are Kconfig options (`src/Kconfig.projbuild`, menuconfig), never code. `sdkconfig.esp32-c3-devkitm-1` is generated from `sdkconfig.defaults` and not committed.

## Rules

- `lib/<name>/` is **pure**: no ESP-IDF or FreeRTOS headers; it must build with host gcc. Libs with logic get a Unity test in `test/test_<name>/` (`model_hook` is a stub, `lib_ignore`d in native). `src/` is **hardware** (drivers, NVS, network, tasks) and is only built for the device.
- `src/` files are listed in `src/CMakeLists.txt`; `lib/` is picked up by PlatformIO's LDF (no `EXTRA_COMPONENT_DIRS`).
- No `vTaskDelay` or busy-wait inside a measurement pass: step times are esp_timer one-shots at absolute times `t0 + Σdur`. The only exception is the single LMP-settle `vTaskDelay` at pass start, after `lmp_configure` and before `t0`.
- Never publish data during a pass. New I2C driver (`driver/i2c_master.h`) only.
- Files derived from LinnesLab (LMP91000, MiniStatAnalyst) keep their MIT notice.

## Architecture (firmware/src)

Boot (`main.c`): `store_init` → I2C bus → `engine_start` (creates `meas_task`) → `lmp_init`, `ads_init(bus, engine_task(), EVT_ADC_RDY)`, `ad5662_init` → safe state → `pub_start` → `net_start(on_cmd)`.

- `engine.c` — `meas_task` (prio 10) runs a command: validate against the active profile → optional autorange explore pass → final pass. Task notification index 0 carries `EVT_*` bits (timers, RDY ISR, stop); index 1 is "publish done" from `pub_task`. After every pass the cell goes to safe state (bias 0 %, VREF 2000 mV, LMP locked).
- `pub.c` — `pub_task` (prio 3) publishes a finished pass in ≤ 4 KB blocks; the pass buffers must stay untouched until it notifies.
- `net.c` — WiFi STA + esp-mqtt; `on_cmd` runs in the esp-mqtt task and must not block. Topics `potenciostato/<device_id>/{cmd,status,data,result}`, QoS 1.
- `store.c` — NVS: up to 4 profiles (one active) and calibration.
