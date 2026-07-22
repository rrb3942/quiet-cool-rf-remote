# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

An ESPHome external component (plus a standalone Arduino sketch) that drives a QuietCool whole-house
fan by transmitting its 433 MHz FSK remote-control protocol through a CC1101 radio attached to an ESP32.
There is no receive path — the device is transmit-only, so it has no feedback about actual fan state.

## Build / run

ESPHome (the primary target). Requires a `secrets.yaml` in the repo root with `wifi_ssid`,
`wifi_password`, `api_enctryption_key` (note the typo — that is the real key name), and `ota_password`:

```
esphome compile quietcool-fan-example.yaml
esphome upload --device /dev/ttyUSB0 quietcool-fan-example.yaml
esphome logs   --device /dev/ttyUSB0 quietcool-fan-example.yaml
```

There are two example configs, and **both must compile** — they exercise different halves of the
`#ifdef USE_ARDUINO` split described under "Vendored code":

- `quietcool-fan-example.yaml` — esp32dev, `framework: arduino`
- `quietcool-fan-esp32s3-example.yaml` — Seeed XIAO ESP32-S3, `framework: esp-idf`

Both point `external_components` at the local `components/` directory, so they compile whatever is in
the working tree. The README's copy-paste config points at the GitHub URL instead — that one builds
from `main`, not from local edits.

Arduino:

```
cd arduino && pio run                 # compile
cd arduino && pio run --target upload
```

There are **no tests**. CI (`.github/workflows/esphome-ci.yml`) is compile-only: it writes a dummy
`secrets.yaml`, runs `esphome compile` on the example YAML, then `pio run` in `arduino/`. A change is
"verified" when both of those compile; anything beyond that needs real hardware.

## Architecture

### The two source trees have diverged — `components/` is canonical

`arduino/src/quietcool.{h,cpp}` and `components/quiet_cool/fan/quietcool.{h,cpp}` share filenames and
ancestry but are **not** the same code and are not kept in sync:

- `components/` version: builds packets from byte arrays, takes a configurable 7-byte `remote_id`,
  configurable frequency/deviation, ESPHome namespaces and `ESP_LOGx`.
- `arduino/` version: an older design with a hardcoded table of 150-character bit strings, one per
  speed/duration combination, with the author's own remote ID baked into every string. Its
  `QuietCoolSpeed` enum even has a different shape (`QUIETCOOL_SPEED_OFF` exists there, not in the
  component).

Protocol work goes in `components/`. Do not assume an edit there reaches the Arduino build, and do not
"fix" one file to match the other without being asked.

### Component layers (`components/quiet_cool/`)

1. **Codegen** — `__init__.py` declares the `quiet_cool` namespace; `fan/__init__.py` defines the YAML
   schema and emits the C++ setter calls (`set_pins`, `set_remote_id`, `set_frequencies`). Adding a YAML
   option means touching the schema, the `to_code` body, and a setter on `QuietCoolFan`.
2. **ESPHome glue** — `fan/quiet_cool.{h,cpp}`. `QuietCoolFan` is a `Component` + `fan::Fan`. It
   advertises 3 discrete speeds and translates Home Assistant's speed float into `QuietCoolSpeed` /
   `QuietCoolDuration` in `control()`. It is deliberately **not** an `spi::SPIDevice` and the component
   has no `spi` dependency: the CC1101 driver does its own software SPI, so all six pins
   (`clk_pin`, `miso_pin`, `mosi_pin`, `cs_pin`, `gdo0_pin`, `gdo2_pin`) are configured on the `fan:`
   block and there is no top-level `spi:` block.
3. **Radio + protocol** — `fan/quietcool.{h,cpp}`. Configures the CC1101 and assembles/sends packets.

### Wire protocol

Packet = 9-byte SYNC (`15 aa aa aa aa aa aa aa aa`) + 7-byte `remote_id` + command byte, repeated
twice + 2 bytes of zero padding. The whole packet is sent 3 times with an 18 ms gap.

The command byte is `speed | duration`: speed is the high nibble (LOW `0x90`, MED `0xA0`, HIGH `0xB0`)
and duration the low nibble (1h `0x01`, 2h `0x02`, 4h `0x04`, 8h `0x08`, 12h `0x0C`, ON `0x0F`,
OFF `0x00`).

`remote_id` is unique per physical remote and is a required YAML option. The default in the code is the
author's own remote; it will not work with anyone else's fan without either decoding their remote (see
the RTL-SDR capture in `recordings/` and the URH screenshots in `images/`) or pairing the ESP as an
additional remote.

Radio config lives in `QuietCool::initCC1101()`: FSK, data rate 2.398 kbps, sync/CRC/whitening/Manchester
all disabled, fixed packet length, `setPktFormat(0)`. The default center frequency is **433.897 MHz**,
not 433.92 — the CC1101 modules used here transmit high, and the offset compensates. If a build stops
working on real hardware, frequency is the first thing to check.

### Vendored code

`components/quiet_cool/fan/ELECHOUSE_CC1101_SRC_DRV.{h,cpp}` is a vendored copy of
`lsatan/SmartRC-CC1101-Driver-Lib`, because ESPHome external components cannot pull PlatformIO
libraries. The Arduino build gets the same library via `lib_deps` in `arduino/platformio.ini`. Treat the
vendored files as third-party — don't refactor them, and if the driver needs changes, prefer working
around it in `quietcool.cpp`.

The **only** edits to those two files are their include blocks: under `esp-idf` there is no `Arduino.h`
or `SPI.h`, so both `#ifdef USE_ARDUINO` down to `fan/arduino_compat.h` instead. That shim supplies just
what the driver touches — `pinMode` / `digitalWrite` / `digitalRead`, `delay`, `delayMicroseconds`,
`map`, `bitRead`, `strlen`, and an `SPI` object that **bit-bangs SPI in software** (mode 0, MSB first).

Software SPI is not an accident: the driver polls MISO as a plain GPIO
(`while (digitalRead(MISO_PIN));`) as the CC1101 chip-ready handshake, which a hardware SPI peripheral
owning that pin makes awkward. Packets are 20 bytes, so there is nothing to gain from the peripheral.
The shim also pulls inputs down, so a missing or unpowered radio reports "not detected" instead of
hanging forever in one of those unbounded waits.

### Load-bearing quirks — do not "fix" these

- `SendData(byte*, byte)` writes a **length byte** into the TX FIFO even though the config is
  fixed-length (`setLengthConfig(0)`, `setPacketLength(20)`). The on-air packet is therefore
  `0x14` + `SYNC(9)` + `remote_id(7)` + `cmd` + `cmd` + one pad byte, with the 20th buffer byte
  truncated. Probably just harmless extra preamble — but it is the exact byte stream the known-good
  esp32dev unit emits, and there is no way to retest a change to it without hardware.
- `setCCMode(1)` sets `IOCFG0 = 0x06` and GDO0 is an **input**, polled by `SendData()` for
  end-of-packet. Any design that drives GDO0 as an output is the async-serial mode, not this one.
