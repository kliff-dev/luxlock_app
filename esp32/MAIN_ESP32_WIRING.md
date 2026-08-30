# LuxLock Main ESP32 Wiring

This wiring plan is for the new 38-pin ESP32 WROOM board used as the main
LuxLock controller.

## Main ESP32 Jobs

- Wi-Fi / Firebase / app communication
- R307S fingerprint sensor
- Three BTS7960 actuator drivers
- UART messages from the tamper ESP32

## Power Rules

- Main ESP32 `5V/VIN` should receive a stable 5V supply.
- Main ESP32 `GND`, BTS7960 logic `GND`, fingerprint `GND`, and tamper ESP32
  `GND` must share common ground.
- Do not connect 12V directly to the ESP32.
- BTS7960 motor power still uses 12V on the driver motor supply terminals.

## Fingerprint Sensor

| Fingerprint Sensor | Main ESP32 |
| --- | --- |
| VCC | 5V |
| GND | GND |
| TX | GPIO16 |
| RX | GPIO17 |

## Tamper ESP32 UART

| Tamper ESP32 | Main ESP32 |
| --- | --- |
| TX GPIO17 | RX GPIO21 |
| RX GPIO16 | TX GPIO22 |
| GND | GND |

The RX line back to the tamper ESP32 is optional for now, but it is useful to
wire it already in case we later send settings or alarm commands back.

## Tamper ESP32 Sensors And Buzzer

The tamper ESP32 reads three ADXL345 sensors and drives the 12V active buzzer
through a 2N2222 transistor.

| ADXL345 Case | Tamper ESP32 Bus | Address Pin |
| --- | --- | --- |
| Case 1 | SDA GPIO21, SCL GPIO22 | SDO/ALT -> GND, address `0x53` |
| Case 2 | SDA GPIO25, SCL GPIO26 | SDO/ALT -> GND, address `0x53` |
| Case 3 | SDA GPIO32, SCL GPIO33 | SDO/ALT -> GND, address `0x53` |

Case 1 and Case 2 use the ESP32 hardware I2C controllers. Case 3 uses a
software I2C bus so every ADXL345 can keep the same default `0x53` address.

ADXL345 power:

| ADXL345 Pin | Connection |
| --- | --- |
| VCC | Tamper ESP32 3.3V |
| GND | Common GND |
| CS | 3.3V if the module requires it for I2C mode |

Active buzzer with 2N2222:

| Part | Connection |
| --- | --- |
| Active buzzer `+` | 12V PSU positive |
| Active buzzer `-` | 2N2222 collector |
| 2N2222 emitter | Common GND |
| 2N2222 base | Tamper ESP32 GPIO27 through a 1k resistor |

The tamper firmware drives GPIO27 HIGH to turn the buzzer on. The buzzer,
transistor emitter, tamper ESP32, and PSU negative must all share common ground.

## BTS7960 Driver Wiring

Each BTS7960 uses only two ESP32 control GPIOs:

- `RPWM`
- `LPWM`

Wire each BTS7960 enable pin permanently HIGH on the driver logic side:

- `R_EN` -> BTS7960 logic `VCC`
- `L_EN` -> BTS7960 logic `VCC`

Do not connect `R_EN` or `L_EN` to ESP32 GPIOs with the current firmware.

The GPIOs are grouped as close pairs so each actuator is easier to wire:

| Case | One-Line Control Pair |
| --- | --- |
| Case 1 | `GPIO25 -> RPWM`, `GPIO26 -> LPWM` |
| Case 2 | `GPIO32 -> RPWM`, `GPIO33 -> LPWM` |
| Case 3 | `GPIO18 -> RPWM`, `GPIO19 -> LPWM` |

Recommended physical grouping:

```text
Case 1 driver control wires: GPIO25, GPIO26
Case 2 driver control wires: GPIO32, GPIO33
Case 3 driver control wires: GPIO18, GPIO19
```

Try to route each pair together with matching colors, for example:

```text
RPWM = yellow
LPWM = orange
GND  = black
VCC  = red
```

## BTS7960 Power Side

| BTS7960 Terminal | Connection |
| --- | --- |
| B+ / VMotor+ | 12V PSU positive |
| B- / VMotor- | 12V PSU negative |
| M+ / M- | Linear actuator wires |
| VCC logic | 5V logic supply |
| GND logic | Common GND |

If an actuator moves in the wrong physical direction, swap the actuator `M+`
and `M-` wires for that BTS7960, or change that case's `openUsesRpwm` value in
the firmware.

## Pins Intentionally Avoided

- GPIO34, GPIO35, GPIO36, GPIO39: input-only, not for actuator output.
- GPIO0, GPIO2, GPIO12, GPIO15: boot strapping pins, avoid for motor drivers.
- GPIO1, GPIO3: USB serial/programming pins, avoid unless necessary.
- GPIO4, GPIO5: avoided in this plan to reduce boot issues.
