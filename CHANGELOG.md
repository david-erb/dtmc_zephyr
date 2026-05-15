# Changelog

## v1.1.0 - 2026-05-15

- **API change**: `dtadc_zephyr_saadc_config_t` gains a `counter_dev` field (`const struct device*`); callers must supply a counter device pointer (e.g. `DEVICE_DT_GET(DT_NODELABEL(timer1))`) when initialising the struct.
- Add `dtinterval_zephyr_counter` module: hardware counter peripheral backend for `dtinterval`, giving microsecond-accurate scan intervals on nRF TIMER peripherals.
- Add `dtinterval_zephyr_ktimer` module: `k_timer`-based backend for `dtinterval`, tick-accurate and suitable where a hardware counter is unavailable.
- Fix ADC scan timestamps, which were incorrect in the previous release.
- Add `benchmark_adc` application that logs an environment table at startup and uses `SYS_CLOCK_TICKS_PER_SEC=32768` for 30 µs timestamp resolution.
- Add `benchmark_interval` application for measuring interval timer accuracy.
- Add `dtmc_services` suite to the `test_dry` runner.
