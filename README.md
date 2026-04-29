# ProFlame 2 ESPHome Component

Pure-local control of your **Heat & Glo / SIT Group ProFlame 2** gas fireplace
from Home Assistant (or HomeKit, via the HA bridge), no cloud — over OOK 314.973
MHz RF using a CC1101 radio.

<img src="/img/Dashboard.jpg"></img>
<img src="/img/proflame2-esp-device.jpeg"></img>

---

## ⭐ The fast path: LilyGo T-Embed CC1101

If you're starting from scratch, **buy a [LilyGo T-Embed CC1101](https://lilygo.cc/products/t-embed-cc1101?variant=44892519137461)**
(or the **T-Embed CC1101 Plus** — same board with an extra PN532 NFC chip
that this firmware leaves dormant).
Then either:

- **[Web-flash from your browser](https://dangerouslaser.github.io/proflame2-esp/)**
  — plug the board into USB, click *Install*, push your Wi-Fi creds via
  Improv-Wi-Fi, pair on-device. **No CLI, no Docker, no ESPHome install.**
  *(Chrome/Edge desktop only.)*
- Or use [`proflame2_tembed.yaml`](./proflame2_tembed.yaml) the regular way
  (clone + `esphome run`) if you want to bake your own secrets, encryption
  key, etc. into the firmware.

Either way, this is the path the project is built around:

- **No wiring** — ESP32-S3, CC1101 radio, ST7789V LCD, rotary encoder + push-
  button, dedicated user button, 8-pixel WS2812 strip, BQ27220 fuel gauge, and
  Li-ion battery on a single board with USB-C charging.
- **Pair in 30 seconds, no SDR.** Long-press the encoder, point your OEM
  remote at the device, press a couple of buttons, hold to confirm.
  → [On-device pairing (T-Embed)](docs/pairing-tembed.md)
- **Standalone LCD + encoder UI** that drives the fireplace without HA or
  Wi-Fi. Idle field list, full settings page (LEDs, battery rendering, clock
  screensaver, encoder direction, backlight timeout, info, clear-pairing,
  reboot), 8-row climate editor (mode / target / fan / heat-mode defaults).
  → [Device UI](docs/device-ui.md)
- **Fire-effect LED strip** along the bottom edge — red / orange / yellow
  flicker that runs whenever the burner is on, killable from HA or the
  device's settings cog.
- **Battery monitoring** via the on-board BQ27220 fuel gauge.
- **HomeKit-friendly** climate (HEAT/OFF thermostat with fan modes), light
  (1–6 brightness as 0–100 %), state restoration, web fallback. Same as
  the generic build — just nothing to wire.

If you already have a CC1101 breakout and an ESP32 dev board, see the
[generic ESP32 path](docs/hardware.md#b-generic-esp32--cc1101-breakout) — same
component, same protocol, manual wiring + manual pairing.

---

## Quickstart

1. **[Pick your hardware](docs/hardware.md)** — T-Embed (recommended) or
   generic ESP32 + CC1101 breakout.
2. **[Install + flash](docs/installation.md)** — drop the `external_components`
   stanza, copy the example YAML, set your secrets, `esphome run`.
3. **[Pair your remote](docs/pairing.md)** — on-device on the T-Embed,
   HA-driven on the plain ESP32, or via SDR for boards without GDO0.
4. **[Pull the batteries from your OEM remote](docs/pairing.md#-after-pairing-pull-the-batteries-from-your-oem-remote)**
   so it doesn't fight the ESP32. (Important — read this before you wonder
   why settings revert.)

---

## Documentation

| | |
|---|---|
| [Hardware](docs/hardware.md) | T-Embed pin map, generic ESP32 wiring |
| [Installation & quickstart](docs/installation.md) | external_components, secrets, flashing |
| [Pairing](docs/pairing.md) | overview + the OEM remote conflict warning |
| · [On-device (T-Embed)](docs/pairing-tembed.md) | 30 s, no SDR, no YAML edits |
| · [On-device (plain ESP32)](docs/pairing-esp32.md) | HA-driven, requires `gdo0_pin` wired |
| · [SDR capture (any board)](docs/pairing-sdr.md) | rtl_433 / OMG / pair-as-new-remote |
| [Climate (thermostat)](docs/climate.md) | HEAT/OFF behavior, HomeKit fan modes |
| [Device UI (T-Embed)](docs/device-ui.md) | LCD + encoder gestures, LED strip, backlight |
| [Standalone web UI](docs/web-ui.md) | HA-free fallback over `web_server:` |
| [Home Assistant entities](docs/entities.md) | full entity list, boot defaults, state restoration |
| [Protocol details](docs/protocol.md) | wire format, ECC math, RX architecture |
| [Troubleshooting](docs/troubleshooting.md) | common symptoms and fixes |

---

## ⚠️ Safety

This component controls a gas appliance. Always keep the original remote
available (with batteries removed but stored — see Pairing), install CO
detectors, follow local codes, and consider timeout automations. Test
thoroughly before relying on it.

---

## Credits

This component stands on a lot of other people's work. In rough order of how
much code / architecture we pulled from each:

- **[merbanan/rtl_433](https://github.com/merbanan/rtl_433)** — the
  [`proflame2.c`](https://github.com/merbanan/rtl_433/blob/master/src/devices/proflame2.c)
  device decoder is the canonical reference for the ProFlame 2 protocol:
  word layout, Thomas-Manchester encoding, the 4-chip `1110` sync, the
  ECC inversion math, parity / guard-bit validation. Our
  `proflame2_decode.{h,cpp}` is a direct port of that logic to host-testable
  C++, with the same field semantics and the same pulse-demod approach.
- **[NorthernMan54/rtl_433_ESP](https://github.com/NorthernMan54/rtl_433_ESP)**
  — the entire RX architecture is modeled on this project. CC1101 register
  values for raw-OOK envelope mode (DATARATE 17.24 kbaud, 270 kHz RX BW,
  `AGCCTRL2 = 0xC7`, etc.), the GDO0 ISR + edge-timestamp ring-buffer
  pattern, and the chip-rate-locked sample-buffer / linear-scan framer all
  come from here. Without it, our first attempt — a streaming edge-time
  classifier — couldn't survive WiFi ISR jitter; once we ported this
  architecture, pairing converged in under two seconds.
- **[johnellinwood/smartfire](https://github.com/johnellinwood/smartfire)**
  — original reverse engineering of the ProFlame 2 RF protocol. The serial
  + ECC pairing model, command-byte layout, and 5-repeat transmit pattern
  all came out of this work.
- **FCC ID `T99058402300`** — the OEM remote's filing has the protocol
  documentation that confirms baud rate, modulation, and frequency.
- **[j2deen/proflame2-esp](https://github.com/j2deen/proflame2-esp)** —
  the original ESPHome external-component for ProFlame 2: component
  skeleton, ESP-IDF framework setup, secondary-flame support, and
  non-blocking TX state machine. **[marksieczkowski/proflame2-esp](https://github.com/marksieczkowski/proflame2-esp)**
  is an intermediate fork of j2deen's; this project began as a fork of
  *that* and has since been detached from the fork network as it
  diverged substantially (ESP32-S3 / T-Embed target, RX path, on-device
  learn-mode, climate, light, state restoration, device UI, LED strip,
  web UI).
- **[LSatan/SmartRC-CC1101-Driver-Lib](https://github.com/LSatan/SmartRC-CC1101-Driver-Lib)**
  — early reference for CC1101 register programming patterns and the
  PA-table values used at 314.973 MHz.
- **[Xinyuan-LilyGO/T-Embed-CC1101](https://github.com/Xinyuan-LilyGO/T-Embed-CC1101)**
  — the official LilyGo hardware reference is where the T-Embed pin map
  came from: shared SPI bus + per-CS muxing, antenna-switch quirks
  (GPIO47 SW1 high / GPIO48 SW0 low for 315 MHz routing), and the
  `GPIO15` peripheral power-enable that has to be HIGH before SPI starts
  or every CC1101 register reads `0x00`.
- **[OpenMQTTGateway](https://github.com/1technophile/OpenMQTTGateway)** —
  an alternate path for capturing the OEM remote's serial + ECC if you
  don't have an SDR and aren't on the T-Embed. OMG embeds rtl_433_ESP and
  decodes ProFlame 2 out of the box.
- **[ESPHome](https://esphome.io/)** — the framework that hosts this
  whole component. The climate / light / number / switch / button entity
  surfaces, NVS persistence, OTA, web server, and codegen pipeline are
  all ESPHome.

---

## License

MIT — see [LICENSE](./LICENSE).

## Disclaimer

Not affiliated with SIT Group, ProFlame, or any fireplace manufacturer.
Use at your own risk.
