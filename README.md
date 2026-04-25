# dtmc_zephyr

Zephyr implementations of the `dtmc_base` porting contracts — GPIO, ADC, UART, CAN bus, Modbus RTU, CoAP, Bluetooth LE, light sensing, and logging — plus a set of demo applications that exercise each one.

`dtmc_zephyr` is part of the **[Embedded Applications Lab](https://david-erb.github.io/embedded)** — a set of working applications based on a set of reusable libraries across MCU, Linux, and RTOS targets.

---

## What's inside

| Area | Headers |
|------|---------|
| GPIO / ADC | `dtgpiopin_zephyr`, `dtadc_zephyr_saadc` |
| Serial / CAN | `dtiox_zephyr_uartirq`, `dtiox_zephyr_canbus` |
| Modbus RTU | `dtiox_zephyr_modbus_slave` |
| Networking | `dtnetportal_coap`, `dtnetportal_btle` |
| Sensors | `dtlightsensor_zephyr` |
| Diagnostics | `dtmc_logging` |

---

## Demo applications

| App | What it shows |
|-----|--------------|
| `demo_gpiopin_blink` / `demo_gpiopin_button` / `demo_gpiopin_record` | GPIO output, interrupt input, and pin recording |
| `demo_adc_saadc` | SAADC one-shot sampling |
| `demo_iox_uart` / `demo_iox_canbus` | UART and CAN I/O streams |
| `demo_iox_modbus` / `demo_iox_ping` | Modbus RTU slave and I/O ping |
| `demo_netportal_modbus` / `demo_netportal_uart` | Netportal over Modbus and UART transports |
| `benchmark_netportal_simplex_modbus` / `benchmark_netportal_duplex_modbus` | Netportal throughput benchmarks |
| `demo_runtime_info` | Runtime diagnostics and task registry |
| `test_dry` / `test_net` / `test_sensors` | Unit and integration test suites |

---

## Dependencies

`dtmc_zephyr` depends on `dtcore`, `dtmc_base`, and `dtmc_services`, which are included as git submodules under `submodules/`. After cloning, initialize them with:

```sh
git submodule update --init --recursive
```

---

## Docs

See the [dtmc_zephyr documentation site](https://david-erb.github.io/dtmc_zephyr/) for the full API reference.
