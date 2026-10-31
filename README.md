# Signal tracker

Firmware for a simple servo-driven rotating platform, controlled with text commands over USB serial.

## Related

- **Router software:** [teltonika-signal-tracker](https://github.com/dominykasssx/teltonika-signal-tracker) — RUTOS packages that drive this firmware from a Teltonika router: scan the positions, measure the mobile signal at each and turn to the best one, with a WebUI.
- **3D-printed stand:** STL files for the turntable on [Thingiverse](https://www.thingiverse.com/thing:7429147).

## Flash it

**From the browser:** open this repo's GitHub Pages site (`https://dominykasssx.github.io/signal-tracker/`), pick your board, click Install.

**With PlatformIO:**

```sh
pio run -e d1_mini -t upload     # build and flash (see platformio.ini for env names)
pio device monitor               # 115200 baud
```

## Boards

| env | Board | Servo signal pin |
|---|---|---|
| `d1_mini` | Wemos D1 mini | D2 (GPIO4) |
| `d1_mini_lite` | Wemos D1 mini lite (ESP8285) | D2 (GPIO4) |
| `nodemcuv2` | NodeMCU v2 / v3 | D2 (GPIO4) |
| `esp01_1m` | ESP-01 / ESP-01S | GPIO2 |
| `esp32dev` | ESP32 | GPIO4 |
| `esp32s2` | ESP32-S2 | GPIO4 |
| `esp32s3` | ESP32-S3 | GPIO4 |
| `esp32c3` | ESP32-C3 | GPIO4 |
| `esp32c5` | ESP32-C5 | GPIO4 |
| `esp32c6` | ESP32-C6 | GPIO4 |
| `esp32h2` | ESP32-H2 | GPIO4 |
| `esp32p4` | ESP32-P4, chip rev. below v3 | GPIO4 |
| `esp32p4_r3` | ESP32-P4, chip rev. v3 or newer | GPIO4 |

## Wiring

| Servo wire | Connect to |
|---|---|
| Brown (GND) | Power supply − **and** the board's GND |
| Red (+) | Separate 5–6 V supply, 2 A or more |
| Orange (signal) | Servo signal pin from the table above |

Don't power the servo from the board's 5V pin: its current spikes reset the ESP. The 3.3 V signal drives the MG995 fine.

## Serial commands

115200 baud, one command per line (newline or CR).

| Send | Does | Reply |
|---|---|---|
| `90` | Turn to 90° (whole numbers 0 to max) | `MOVING 135 -> 90`, then `AT 90` once stopped |
| `s10` | Speed in ms per degree (3–1000, default 15) | `SPEED 10` |
| `m180` | Max: how far the servo turns (10–360, default 270) | `MAX 180` |
| `r0` / `r1` | Direction: `r1` turns the other way (default `r1`) | `REVERSE 0` |
| `?` | Status | `POS 90 TARGET 90 SPEED 15 MAX 270 REVERSE 1` |

Bad input gets an `ERR ...` line and changes nothing. `AT` comes 300 ms after the last step, so when automating a sweep, wait for it before reading the signal.

Max and direction are saved on the board and kept across restarts. Changing either doesn't move the platform: the current position is converted to the new setting, so `?` may report a different angle afterwards.
