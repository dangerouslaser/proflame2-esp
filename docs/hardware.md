# Hardware

[← Back to README](../README.md)

Pick the hardware path you want to use. The T-Embed is faster to set up and
unlocks the on-device features; the generic ESP32 path is for makers who
already have an ESP32 dev board and a CC1101 breakout.

## A. LilyGo T-Embed CC1101 (recommended)

All-in-one: ESP32-S3 with integrated CC1101 radio, 1.9" ST7789V LCD, rotary
encoder + push-button, dedicated user button, 8-pixel WS2812 strip on the
bottom edge, 8 MB OPI PSRAM, 16 MB flash, BQ27220 fuel gauge + Li-ion battery,
USB-C charging.

**Both LilyGo variants work identically with this firmware:**

- **T-Embed CC1101** — the base board.
- **T-Embed CC1101 Plus** — adds a PN532 NFC/RFID transceiver on the I²C bus
  (address `0x24`). The proflame2 firmware never touches it (`i2c.scan: false`,
  no platform binding), so the extra silicon is dormant and harmless.

Same pin map, same example YAML, same flash flow on both.

- No wiring — everything is on the same PCB.
- Hardware reference: [Xinyuan-LilyGO/T-Embed-CC1101](https://github.com/Xinyuan-LilyGO/T-Embed-CC1101).
- Example YAML: [`proflame2_tembed.yaml`](../proflame2_tembed.yaml).

### Pin map (verified from LilyGo's repo)

| Function | GPIO | Function | GPIO |
|---|---|---|---|
| CC1101 CS | 12 | ST7789V CS | 41 |
| CC1101 GDO0 | 3 | ST7789V DC | 16 |
| CC1101 GDO2 | 38 | ST7789V RST | 40 |
| Shared MOSI | 9 | ST7789V BL | 21 |
| Shared MISO | 10 | Encoder A | 4 |
| Shared SCLK | 11 | Encoder B | 5 |
| | | Encoder Btn | 0 |
| | | User key | 6 |

The CC1101 needs three additional pins driven before any SPI traffic, otherwise
every register read returns `0x00`:

- `GPIO15` — peripheral power-enable (driven HIGH).
- `GPIO47` — antenna switch SW1 (HIGH for 315 MHz routing).
- `GPIO48` — antenna switch SW0 (LOW for 315 MHz routing).

The example YAML wires these up automatically.

## B. Generic ESP32 + CC1101 breakout

For makers with parts on hand. ProFlame 2 transmits at 314.973 MHz, so the
**433 MHz** CC1101 variant is the right choice — its synthesizer tunes down
to the ProFlame band.

- ESP32 development board (ESP-IDF framework).
- CC1101 433 MHz module.
- Jumper wires.
- USB power (3.3 V from the ESP32 is enough for the CC1101).
- Example YAML: [`proflame2_fireplace.yaml`](../proflame2_fireplace.yaml).

### Wiring

```
ESP32          CC1101
-----          ------
3.3V    <-->   VCC
GND     <-->   GND
GPIO21  <-->   CSN  (Chip Select)
GPIO18  <-->   SCK
GPIO23  <-->   MOSI
GPIO19  <-->   MISO  (TX-only would technically work without this)
GPIO22  <-->   GDO0  (used as TX-FIFO threshold indicator AND the RX edge
                      pin for on-device pairing — wire it up!)
              GDO2   (not connected)
```

Adjust the GPIO numbers in `proflame2_fireplace.yaml` if your wiring differs.
On-device pairing requires `gdo0_pin` to be wired; without it, the receiver is
unavailable and pairing falls back to the SDR flow.
