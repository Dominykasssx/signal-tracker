# Signal Finder

Firmware for a rotating platform: an ESP8266 or ESP32 drives an MG995 servo (270° or 180°), and you send it angles over USB serial. Mount a router on top, turn it, read the signal at each angle, keep the best one.

## Flash it

**From the browser:** open this repo's GitHub Pages site (`https://<owner>.github.io/<repo>/`) in Chrome or Edge, pick your board, click Install.

**With PlatformIO:**

```sh
pio run -e d1_mini -t upload     # build and flash (see platformio.ini for env names)
pio device monitor               # 115200 baud
```

Every build also writes `.pio/build/<env>/signal_finder-<env>.bin`, a single image that flashes at offset 0:

```sh
esptool.py write_flash 0x0 signal_finder-<env>.bin
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

ESP32 images are built for 4 MB flash, so they run on boards with more. On chips with their own USB port (S2, S3, C3, C5, C6, H2, P4) the firmware answers on both the native USB port and UART0, so it works whichever one the board's USB socket is connected to.

ESP32-C2 and ESP32-C61 are missing because Arduino-ESP32 3.3 doesn't ship prebuilt libraries for them.

## Wiring

| MG995 wire | Connect to |
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
| `?` | Status | `POS 90 TARGET 90 SPEED 15 MAX 270` |

Bad input gets an `ERR ...` line and changes nothing. `AT` comes 300 ms after the last step, so when automating a sweep, wait for it before reading the signal.

The servo turns one degree per step (about 4 s for the full 270° by default) so the router isn't jerked around. At power-up it goes to the middle (135° with max 270) at full speed, since it can't know where it was.

Max is the servo's full travel, the angle the 500–2500 µs pulse range turns it through: 270 for a 270° MG995, `m180` for the common 180° one. The board saves it in flash, so it's kept across restarts and firmware updates. Erasing the flash, or installing an ESP32 image from the web flasher, resets it to 270; check with `?`. Changing max doesn't move the platform: the current position is converted to the new scale.

## CI and the web flasher

[.github/workflows/firmware.yml](.github/workflows/firmware.yml) builds every `[env:*]` in `platformio.ini` on each push and pull request; each build's image is attached to the run as an artifact. On the default branch it then publishes [web/index.html](web/index.html) together with all images to GitHub Pages.

One-time setup: **Settings → Pages → Build and deployment → Source: GitHub Actions.**

To add a board, add an env to `platformio.ini` with `custom_board_name` (and `custom_servo_pin` if it isn't GPIO4). CI and the web flasher pick it up from there.
