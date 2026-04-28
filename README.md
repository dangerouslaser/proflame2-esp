# ProFlame 2 ESPHome Component for ESP32 + CC1101

Control your ProFlame 2 gas fireplace from Home Assistant (and HomeKit, if you
bridge HA into HomeKit) using an ESP32 + CC1101 RF module. Pure-local, no cloud.

Two hardware paths supported:

- **Generic ESP32 + breakout CC1101** — wire it up yourself, control via HA.
  Example: [`proflame2_fireplace.yaml`](./proflame2_fireplace.yaml).
- **LilyGo T-Embed CC1101** (ESP32-S3 with integrated CC1101 + 1.9" LCD +
  rotary encoder + battery) — no wiring, plus a standalone physical UI that
  works when HA is offline AND on-device learn-mode pairing (no SDR needed).
  Example: [`proflame2_tembed.yaml`](./proflame2_tembed.yaml).

This fork (`dangerouslaser/proflame2-esp`) adds:

- A real `climate` entity (HEAT/OFF thermostat) so HA's HomeKit bridge can
  expose the fireplace as a Thermostat / HeaterCooler accessory with the room
  temperature and a target setpoint — instead of a generic "input number" for
  flame height.
- The fireplace light is now a real `light` entity (brightness 0–100% maps to
  fireplace levels 1–6), so HomeKit gets a Light accessory with a brightness
  slider — and reflects the hardware constraint that the light only operates
  while the burner is running.
- Three persistent config entities for tuning the climate's auto-activation
  behavior: **Heat Flame Level**, **Heat Fan Level**, **Heat Secondary Flame**.
  Adjust at runtime in HA — no reflash.
- The two ECC pairing-constant pairs that go at the end of every TX packet are
  now YAML-configurable (`ecc_constants:`) instead of hardcoded — see
  [Pairing your remote](#pairing-your-remote).
- **T-Embed-only:** standalone LCD + rotary encoder UI (operates the fireplace
  without HA / WiFi) and on-device learn-mode pairing that recovers the serial
  number + ECC constants from your existing OEM remote — no SDR, no rtl_433,
  no manual YAML edits. See [Pairing on-device](#pairing-on-device-t-embed-cc1101).

Inherited from prior forks (marksieczkowski → j2deen): ESP-IDF framework,
secondary flame support, non-blocking TX state machine, send button.

## Features

- ✅ Native Home Assistant integration via ESPHome
- ✅ HEAT/OFF thermostat with HA-side temperature sensor (HomeKit-friendly)
- ✅ Fireplace light as a real `light` entity (HomeKit Light)
- ✅ Persistent heat-mode config (flame / fan / secondary flame)
- ✅ All pairing-specific constants (serial number + ECC) are YAML-configurable
- ✅ Original manual entities preserved: power, pilot mode, aux, secondary
  flame, flame height (0–6), fan speed (0–6), send button
- ✅ No cloud dependency, fully local
- ✅ Optional ESPHome `web_server` for standalone control

## Hardware Requirements

Pick **one** of:

**A. Generic ESP32 + CC1101 breakout**
- ESP32 development board (ESP-IDF framework)
- CC1101 RF module (the **433 MHz** variant — the same hardware tunes down to
  314.973 MHz for ProFlame 2)
- Jumper wires
- USB power (3.3 V from the ESP32 is enough for the CC1101)

**B. LilyGo T-Embed CC1101**
- All-in-one: ESP32-S3, integrated CC1101, 1.9" ST7789V LCD, rotary encoder
  + push-button, 8 MB OPI PSRAM, 16 MB flash, battery + USB-C charging.
- No wiring — everything is on the same PCB. Just flash and pair.

## Wiring

For path **A** (generic ESP32 + CC1101 breakout):

```
ESP32          CC1101
-----          ------
3.3V    <-->   VCC
GND     <-->   GND
GPIO21  <-->   CSN  (Chip Select)
GPIO18  <-->   SCK
GPIO23  <-->   MOSI
GPIO19  <-->   MISO  (TX-only would technically work without this)
GPIO22  <-->   GDO0  (used as TX-FIFO threshold indicator)
              GDO2  (not connected)
```

For path **B** (T-Embed CC1101): no wiring required. Pin map (verified
against the official LilyGo repo):

| Function | GPIO | Function | GPIO |
|---|---|---|---|
| CC1101 CS | 12 | ST7789V CS | 41 |
| CC1101 GDO0 | 3 | ST7789V DC | 16 |
| CC1101 GDO2 | 38 | ST7789V RST | 40 |
| Shared MOSI | 9 | ST7789V BL | 21 |
| Shared MISO | 10 | Encoder A | 4 |
| Shared SCLK | 11 | Encoder B | 5 |
|  |  | Encoder Btn | 0 |

## Installation

```yaml
external_components:
  - source: github://dangerouslaser/proflame2-esp@main
    components: [proflame2]
```

A complete example lives in [`proflame2_fireplace.yaml`](./proflame2_fireplace.yaml).

## Minimum configuration

```yaml
spi:
  clk_pin: GPIO18
  miso_pin: GPIO19
  mosi_pin: GPIO23

proflame2:
  id: fireplace
  cs_pin: GPIO21
  gdo0_pin: GPIO22
  serial_number: !secret fireplace_remote_serial_number
  ecc_constants:                        # see "Pairing your remote" below
    c1: 0x08
    d1: 0x0E
    c2: 0x0B
    d2: 0x07

  power:           { name: "Power",            icon: "mdi:fireplace" }
  pilot:           { name: "Pilot Mode",       icon: "mdi:fire", entity_category: config }
  aux:             { name: "Aux Power",        icon: "mdi:power-plug" }
  flame:           { name: "Flame Height",     icon: "mdi:fire", mode: slider }
  fan:             { name: "Fan Speed",        icon: "mdi:fan",  mode: slider }
  light:           { name: "Fireplace Light",  icon: "mdi:lightbulb" }
  secondary_flame: { name: "Secondary Flame",  icon: "mdi:fire" }
```

Setters auto-transmit on every change — no manual "Send Commands" button is
needed. The new climate + heat-config entities are **additive** — you can
ignore them entirely if you want the simple manual UI.

## Climate (thermostat) configuration

The climate entity needs a current-temperature source from HA. Use a template
helper for indirection (so you can repoint the source without reflashing):

In Home Assistant: **Settings → Devices & Services → Helpers → Create Helper
→ Template a sensor**. Set state template to e.g. `{{ states('sensor.my_ecobee_temperature') }}`
and unit `°F` (or whatever your sensor reports).

Then in your ESPHome YAML:

```yaml
sensor:
  - platform: homeassistant
    id: room_temp
    entity_id: sensor.fireplace_room_temperature   # the helper above
    filters:
      - lambda: 'return (x - 32.0f) * 5.0f / 9.0f;'  # °F → °C (ESPHome climate is °C internally)

proflame2:
  # ... (entities from above) ...
  heat_flame_level:
    name: "Heat Flame Level"
    icon: "mdi:fire"
    entity_category: config
  heat_fan_level:
    name: "Heat Fan Level"
    icon: "mdi:fan"
    entity_category: config
  heat_secondary_flame:
    name: "Heat Secondary Flame"
    icon: "mdi:fire"
    entity_category: config
    restore_mode: RESTORE_DEFAULT_OFF
  climate:
    name: "Fireplace"
    sensor: room_temp
    visual:
      min_temperature: 60°F
      max_temperature: 85°F
      temperature_step:
        target_temperature: 0.5°F
        current_temperature: 0.1°F
```

### How the climate behaves

It's an intentionally simple bang-bang thermostat:

- **HEAT mode**: when current < target − hysteresis (default 0.5 °C), the
  climate sends a single packet that turns power on, sets `flame_level`,
  `fan_level`, and `secondary_flame` to the values held in the three
  `heat_*` config entities, and queues a transmit.
- When current ≥ target + hysteresis, the climate sends a packet that turns
  power off and zeroes fan + secondary flame so the fireplace doesn't sit
  blowing room-temperature air.
- The heat-mode config values are applied **on every burner-on cycle**, not
  just the first one. To change behavior persistently, change the config
  entities — the next cycle picks up the new values.
- **Hysteresis tick** is 5 s (climate `loop()` won't re-evaluate more often
  than that).
- **OFF mode**: the climate also drops fan + secondary along with power, so
  switching the climate to OFF is a clean stop.

The climate exposes built-in fan modes (OFF / LOW / MEDIUM / HIGH → levels
0 / 2 / 4 / 6). HA's HomeKit bridge requires built-in modes (not custom
strings) to surface a RotationSpeed slider on the climate accessory; the
existing `fan` Number entity is still available for fine-grained 0–6 control.

## Light entity

The fireplace light is a brightness-only `light::LightOutput`. Brightness
1–100 % rounds to fireplace levels 1–6.

**Hardware constraint**: the fireplace's light only physically operates while
the burner is running. The component enforces this in two places:

1. `ProFlame2Light::write_state()` rejects "on" attempts when `power` is off,
   logs a warning, and snaps the entity back to off.
2. `ProFlame2Component::set_power(false)` zeros the cached light level and
   forces the light entity off, so HA/HomeKit reflect reality whenever the
   fireplace shuts down (manually or via climate).

## Pairing on-device (T-Embed CC1101)

If you're on the T-Embed build, the easiest path is on-device learn-mode —
no SDR, no rtl_433, no YAML editing. The device sniffs your existing OEM
remote and recovers serial + all four ECC constants algebraically.

1. Flash `proflame2_tembed.yaml` with **any** placeholder `serial_number` /
   `ecc_constants` values — they're seed defaults that will be overridden.
2. Boot the device. The LCD shows the normal state screen.
3. **Hold the encoder button for 1.5 s.** The screen switches to "PAIRING
   — Press a button on the OEM remote".
4. With your OEM remote ~30 cm from the device, press any button (power,
   flame up, light, whatever). The device displays the captured serial and
   ECC values, plus a "valid packets: N/3" counter. Press 2 more times so
   the counter reaches 3/3.
5. The screen switches to "Hold button to confirm" with the candidate
   values displayed in green. **Hold the encoder button** to commit. Short-
   press cancels.
6. The screen briefly shows "Saved." and returns to the normal state.

Done. The pairing is in NVS and survives reboots. `dump_config()` after the
next reboot will report `Serial Number: 0x… (NVS v1 (learned))`.

If 60 s elapses with fewer than 3 agreeing packets the screen shows
"Pairing failed — Press to retry"; press once to dismiss, hold to start
over.

**Safety gates** (gas appliance — these all have to hold simultaneously
before the device will let you confirm):
- 3+ valid packets within a single 60 s capture window.
- All packets agree on serial AND all four ECC constants byte-for-byte.
- Each packet's checksum cross-validates against the inferred (c, d).
- Confirmation is a hold, not a tap, so a stray bump can't commit.

If any of those fail, the device discards everything and re-listens.

## Pairing via SDR (any board)

The traditional path — works on every supported board, including the generic
ESP32 + CC1101. Two pieces of information need to match what your physical
remote was sending, or the fireplace will ignore your packets.

### 1. Serial number (24 bits)

Bytes 0–2 of the packet. Set in YAML via `serial_number:`. Default is
`0x12345678`, which is fine if you're going to **pair the ESP32 as a new
remote** (your old remote will stop working — see below). Otherwise you need
to capture the serial of your existing remote and set it explicitly.

### 2. ECC pairing constants (4 × 4 bits)

Bytes 5–6 of the packet (`err1`, `err2`) are computed from `cmd1`/`cmd2` via:

```
X = (c ^ (hi<<1) ^ hi ^ (lo<<1)) & 0xF
Y = (d ^ hi ^ lo) & 0xF
err = (X << 4) | Y
```

`(c1, d1)` is used for `err1`, `(c2, d2)` for `err2`. These are pairing-time
constants — different remotes paired to different fireplaces have different
`(c, d)`. They go in YAML:

```yaml
proflame2:
  ecc_constants:
    c1: 0x08
    d1: 0x0E
    c2: 0x0B
    d2: 0x07
```

The defaults shipped in this fork are for the maintainer's pairing
(serial `0x320A02`). **They almost certainly won't work for your fireplace
unaltered.**

### Deriving (c, d) from a captured packet

The math is invertible. For any captured frame you have `cmd_byte` (the input)
and `err_byte` (the output):

- Let `hi = cmd_byte >> 4`, `lo = cmd_byte & 0xF`, `X = err_byte >> 4`,
  `Y = err_byte & 0xF`.
- Then `c = X ^ (hi<<1) ^ hi ^ (lo<<1)` (mask to `& 0xF`)
- And `d = Y ^ hi ^ lo` (mask to `& 0xF`)

Special case: a frame with `cmd_byte = 0x00` makes the equation degenerate to
`err_byte = (c << 4) | d` — so a zero-cmd capture gives you `(c, d)` directly.

### Capture options

1. **Use your existing remote** with [rtl_433](https://github.com/merbanan/rtl_433)
   and an [RTL-SDR](https://www.rtl-sdr.com/buy-rtl-sdr-dvb-t-dongles/):
   ```
   rtl_433 -f 314973000 -R 207
   ```
   The decoder prints serial + cmd1/err1 + cmd2/err2 for every press. Toggle
   different controls (power, fan, light) to capture multiple `(cmd, err)`
   pairs and verify your derived constants.
2. **OpenMQTTGateway** (OMG) on a separate ESP32 also decodes ProFlame 2.
3. **Pair as a new remote** (will *replace* your existing remote — only one
   remote can be paired at a time): pick any 24-bit serial and any
   `(c1, d1, c2, d2)`, put the fireplace receiver in pairing mode (see your
   fireplace manual), send a command from the ESP32, and the receiver will
   accept whatever it sees.

## Home Assistant entities

After flashing, expect:

| Entity | Type | Notes |
|---|---|---|
| `switch.fireplace_power` | switch | manual on/off |
| `switch.fireplace_pilot_mode` | switch | IPI/CPI |
| `switch.fireplace_aux_power` | switch | aux outlet |
| `switch.fireplace_secondary_flame` | switch | manual secondary flame |
| `number.fireplace_flame_height` | number | 0–6, manual override |
| `number.fireplace_fan_speed` | number | 0–6, manual override |
| `light.fireplace_light` | light | brightness 1–100 % → level 1–6, gated on power |
| `button.fireplace_send_commands` | button | flush queued state |
| `climate.fireplace` | climate | HEAT/OFF thermostat with fan modes |
| `number.fireplace_heat_flame_level` | number (config) | 1–6, used at burner-on |
| `number.fireplace_heat_fan_level` | number (config) | 0–6, used at burner-on |
| `switch.fireplace_heat_secondary_flame` | switch (config) | applied at burner-on |

## Protocol details

| | |
|---|---|
| **Frequency** | 314.973 MHz (some variants use 315 MHz or 433 MHz) |
| **Modulation** | OOK (On-Off Keying) |
| **Baud rate** | 2400 |
| **Encoding** | Thomas Manchester |
| **Packet** | 7 words × 13 bits = 91 bits per frame, 5 repeats per command, ≥200 ms between commands |
| **Word layout** | sync(1) + start guard(1) + data(8) + pad(1) + parity(1) + end guard(1) + 1 |

Word order:

| word | content | source |
|---|---|---|
| 0 | `serial1`  (high byte of serial number) | YAML `serial_number` |
| 1 | `serial2`  (mid byte) | YAML `serial_number` |
| 2 | `serial3`  (low byte) | YAML `serial_number` |
| 3 | `cmd1`     (pilot/light/thermostat/power) | live state |
| 4 | `cmd2`     (secondary/fan/aux/flame) | live state |
| 5 | `err1`     | computed from cmd1 + `(c1, d1)` |
| 6 | `err2`     | computed from cmd2 + `(c2, d2)` |

`err1` / `err2` are the **remote-specific** part of the packet — that's where
the pairing identity is encoded.

## Troubleshooting

- **Fireplace ignores packets** → 90 % of the time this is wrong serial or
  wrong ECC `(c, d)`. Capture a frame with rtl_433 and verify both.
- **Compile fails** → ESPHome ≥ 2024.7 needed for the climate fan-mode work;
  the climate entity also requires the ESP-IDF framework (Arduino is
  untested here).
- **Light won't turn on** → by design, only when `power` is on.
- **HomeKit fan slider snaps from 0 % to LOW** → known limitation of HA's
  HomeKit bridge; it converts the slider via `percentage_to_ordered_list_item`
  with `[LOW, MIDDLE, MEDIUM, HIGH]` and has no off step.

## Safety

⚠️ **This controls a gas appliance.** Always keep the original remote
available, install CO detectors, follow local codes, and consider timeout
automations. Test thoroughly before relying on it.

## Credits

- Original protocol reverse engineering: [johnellinwood/smartfire](https://github.com/johnellinwood/smartfire)
- ProFlame 2 protocol documentation: FCC ID T99058402300
- ESPHome CC1101 examples: [LSatan/SmartRC-CC1101-Driver-Lib](https://github.com/LSatan/SmartRC-CC1101-Driver-Lib)
- Upstream forks: [j2deen/proflame2-esp](https://github.com/j2deen/proflame2-esp)
  → [marksieczkowski/proflame2-esp](https://github.com/marksieczkowski/proflame2-esp)

## License

MIT — see [LICENSE](./LICENSE).

## Disclaimer

Not affiliated with SIT Group, ProFlame, or any fireplace manufacturer. Use
at your own risk.
