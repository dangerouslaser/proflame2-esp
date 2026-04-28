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
- All pairing-time constants (24-bit serial + four 4-bit ECC values) are
  YAML-configurable (`ecc_constants:`) and override-able via NVS. The on-device
  learn-mode flow writes them automatically — see
  [Pairing your remote](#pairing-on-device-t-embed-cc1101).
- Setters auto-transmit on every change — no manual "Send Commands" button.
- Smart state restoration: light level and secondary-flame state are
  remembered across power cycles. Toggling the fireplace OFF and back ON
  brings them back where you left them. You can even pre-dial them while
  power is off (HA-side and device-side).
- **T-Embed-only**:
  - Standalone LCD + rotary encoder UI that operates the fireplace without
    HA or WiFi.
  - On-device learn-mode pairing that recovers serial + ECC algebraically
    from your existing OEM remote — no SDR, no rtl_433, no YAML edits.
  - 8-pixel WS2812 strip on the bottom edge animates as a fire (red /
    orange / yellow flicker) when the burner is on. Disable from HA or
    via the device's settings cog.
  - LCD backlight auto-dims after 30 s of no input; wakes on any encoder
    or button press.
- **Web server fallback** (any board with `web_server:` configured): the
  ESPHome dashboard at `http://<device>/` is a fully usable manual UI when
  HA is unreachable. See [Standalone web UI](#standalone-web-ui).

Inherited from prior forks (marksieczkowski → j2deen): ESP-IDF framework,
secondary flame support, non-blocking TX state machine.

## Features

- ✅ Native Home Assistant integration via ESPHome
- ✅ HEAT/OFF thermostat with HA-side temperature sensor (HomeKit-friendly)
- ✅ Fireplace light as a real `light` entity (HomeKit Light)
- ✅ Persistent heat-mode config (flame / fan / secondary flame)
- ✅ State restoration: light level + secondary flame remembered across
  power cycles, pre-dial both while power is off
- ✅ On-device learn-mode pairing (T-Embed): no SDR / no rtl_433 needed
- ✅ Standalone LCD + rotary encoder UI that works without HA or WiFi
- ✅ Animated WS2812 fire effect on the T-Embed's bottom-edge LED strip
- ✅ Standalone ESPHome `web_server` UI as a no-HA fallback
- ✅ Manual entities: power, pilot mode, aux, secondary flame, flame height
  (0–6), fan speed (0–6), light (1–6 brightness)
- ✅ No cloud dependency, fully local

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

### Pick the right example YAML for your board

Two ready-to-flash example configs ship in the repo. They use the same
`proflame2:` component but declare different pin maps and entity sets to
match each board:

| Your hardware | YAML to use | Notes |
|---|---|---|
| Plain ESP32 dev board + CC1101 breakout | [`proflame2_fireplace.yaml`](./proflame2_fireplace.yaml) | TX works out of the box. Wire `gdo0_pin` to enable on-device pairing (RX). |
| LilyGo T-Embed CC1101 | [`proflame2_tembed.yaml`](./proflame2_tembed.yaml) | All-in-one — adds LCD UI, rotary encoder, battery sensor, pair button. Pin map is fixed by the board. |

Each YAML carries a header block reiterating the target hardware and pin
expectations — read those before adapting either to your wiring.

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

**Hardware constraint**: the fireplace's light only physically operates
while the burner is running. The HA `light` entity reflects that reality —
it goes off whenever the fireplace powers down and refuses an "on" attempt
while power is off (with a warning in the log).

**State restoration**: the *level* you had selected is remembered across
power cycles. If you had the light at 4, power off, then power on, it
comes back at 4 — the HA entity, the device LCD, and the RF state all
agree. You can also pre-dial a level via either surface while power is
off; it'll apply on the next power-on.

The same restoration applies to **Secondary Flame**: toggling the
fireplace off doesn't lose your "secondary on/off" preference.

## Boot defaults

A fresh boot (or one that hasn't seen any user input yet) starts with:

| Field | Default | Why |
|---|---|---|
| Power | OFF | Never auto-light a gas appliance. |
| Flame Height | 6 | Useful burner level out of the box. |
| Fan Speed | 6 | Match flame so the first power-on isn't silent. |
| Secondary Flame | ON | Both burners light unless explicitly turned off. |
| Fireplace Light | 0 | Opt-in; users typically set it per session. |
| Pilot Mode | IPI | Standard for most installations. |
| Aux Power | OFF | |

Once you change any of these, the latest value is what gets restored on
the next power-on (per-field, see "State restoration" above).

## Device UI (T-Embed CC1101)

The T-Embed build ships with a self-contained LCD + rotary encoder UI
that operates the fireplace independently of HA / WiFi.

**Idle screen** shows the current state of every controllable field
plus a status bar (HA connectivity, battery, signal). The selection
cursor highlights one field at a time.

**Encoder gestures**:
- **Rotate** in *navigate* mode — moves the cursor through fields in
  order: FLAME → FAN → LIGHT → SEC FLAME → POWER → LEDs ⚙ → INFO.
- **Click** a binary field (POWER, SEC FLAME, LEDs) — toggles it.
- **Click** a numeric field (FLAME, FAN, LIGHT) — enters *edit* mode.
- **Rotate** in *edit* mode — adjusts the field's value (clamped 0–6).
- **Click** to exit *edit* mode back to *navigate*.
- **Click** INFO — opens the info screen (serial, ECC, signal strength,
  IP, uptime). Any rotation dismisses it.

**Pre-dial while off**: LIGHT and SEC FLAME stay editable even when
power is off. The values are stashed and applied on the next power-on,
so you can dial in your preferred setup before lighting the fireplace.

**Settings cog (LEDs)**: toggles the bottom-edge WS2812 fire animation.
Same state as the HA `Status LEDs Enabled` switch — flipping one
flips the other.

**Pair button** (T-Embed CC1101's dedicated user button, GPIO 6):
long-press to start learn-mode. Same effect as a long-press of the
encoder button.

**Backlight**: dims off after 30 s of no input; the next encoder turn
or button press wakes it instantly.

## Standalone web UI

Every build configured with `web_server:` exposes the ESPHome dashboard
at `http://<device-name>.local/` (or via the device's IP). It mirrors
every entity the device exposes — POWER, FLAME, FAN, LIGHT, SEC FLAME,
LEDs, climate, and the pair / confirm / cancel buttons — with toggles,
sliders, and a brightness slider for the light. **No Home Assistant
required**; it's a fully usable manual fallback when HA is offline or
when you don't run HA at all.

The example YAMLs ship with HTTP basic auth enabled — set the password
via the `fireplace_remote_web_password` secret. Strip the `auth:` block
if you'd rather not require credentials on a trusted LAN.

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

## Pairing on-device (plain ESP32 + CC1101)

Same algebraic recovery, just driven from Home Assistant instead of an
on-device encoder. Requires `gdo0_pin` wired up — RX is unavailable
without it.

1. Flash `proflame2_fireplace.yaml` with placeholder `serial_number` /
   `ecc_constants`. Make sure `gdo0_pin` is set in YAML.
2. In Home Assistant, find the three pairing buttons exposed by the
   device: **Pair Remote**, **Confirm Pairing**, **Cancel Pairing**.
3. Click **Pair Remote**. The device starts a 60 s listening window;
   ESPHome logs say `Learn-mode armed — press any button on the OEM remote`.
4. Press any button on your OEM remote (power, flame up, etc.) 3 times
   within ~30 cm of the device. Logs report `N/3 valid packets agree`.
5. Once the log shows `CONVERGED — awaiting user confirm: serial=0x…`,
   click **Confirm Pairing**. The captured serial + ECC are committed to
   NVS, and the device logs `Learned values committed`.
6. Click **Cancel Pairing** at any point to abort the listening window
   without committing.

The same safety gates from the T-Embed flow apply — packets must agree
byte-for-byte, checksum cross-validates, etc. The only difference is the
*confirm* step: T-Embed requires a physical encoder long-press, plain
ESP32 trusts the HA button click. If you'd rather not expose the
`pair_confirm` button in HA, you can omit it from your YAML and instead
write a YAML automation that calls `learn_confirm()` after some other
trigger (e.g. a separate physical GPIO button).

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

After flashing, expect (entities you didn't enable in YAML are simply
not exposed):

**Manual controls**

| Entity | Type | Notes |
|---|---|---|
| `switch.fireplace_power` | switch | manual on/off |
| `number.fireplace_flame_height` | number | 0–6 |
| `number.fireplace_fan_speed` | number | 0–6 |
| `light.fireplace_light` | light | brightness 1–100 % → level 1–6, gated on power |
| `switch.fireplace_secondary_flame` | switch | secondary burner |
| `switch.fireplace_aux_power` | switch | aux outlet |
| `switch.fireplace_pilot_mode` | switch | IPI/CPI |

**Climate (optional)**

| Entity | Type | Notes |
|---|---|---|
| `climate.fireplace` | climate | HEAT/OFF thermostat with fan modes |
| `number.fireplace_heat_flame_level` | number (config) | 1–6, used at burner-on |
| `number.fireplace_heat_fan_level` | number (config) | 0–6, used at burner-on |
| `switch.fireplace_heat_secondary_flame` | switch (config) | applied at burner-on |

**Pairing (when learn-mode is wired up)**

| Entity | Type | Notes |
|---|---|---|
| `button.fireplace_pair_remote` | button | start a 60 s capture window |
| `button.fireplace_confirm_pairing` | button | commit captured serial+ECC to NVS |
| `button.fireplace_cancel_pairing` | button | abort the capture |

**T-Embed-only (LED strip)**

| Entity | Type | Notes |
|---|---|---|
| `light.proflame2_tembed_status_leds` | light | manual control of the bottom-edge strip + effects |
| `switch.proflame2_tembed_status_leds_enabled` | switch (config) | master enable for the auto fire animation |
| `sensor.proflame2_tembed_battery` | sensor (diagnostic) | BQ27220 fuel gauge state-of-charge |

The HA entity IDs above use the example-YAML names (`fireplace`,
`proflame2_tembed`); yours will follow whatever `name:` you set in
each YAML's top-level `esphome:` block.

## Protocol details

| | |
|---|---|
| **Frequency** | 314.973 MHz (some variants use 315 MHz or 433 MHz) |
| **Modulation** | OOK (On-Off Keying) |
| **Baud rate** | 2400 |
| **Encoding** | Thomas Manchester |
| **Packet** | 7 words × 13 bits = 91 bits per frame, 5 repeats per command, ≥200 ms between commands |
| **Word layout** | sync(1) + start guard(1) + data(8) + pad(1) + parity(1) + end guard(1) + 1 |

### RX architecture (learn-mode)

The receive path mirrors `rtl_433_ESP`'s strategy: the CC1101 is configured at
DATARATE 17.24 kbaud and 270 kHz RX bandwidth — well above the 2400-baud chip
rate — so it acts as a raw OOK envelope detector rather than a clock-recovery
demodulator. GDO0 fires an ISR on every envelope edge; edge timestamps land in
a 1024-entry SPSC ring; on the inter-burst silence (>6 ms) we round each
pulse-width to chip-count steps and pack it into a chip-rate-locked bit
buffer. A linear scan of the buffer looks for the 4-chip `1110` sync,
Manchester-decodes 11 bits per word, and validates the 7-word ProFlame frame.
This is fundamentally more robust than per-edge streaming decode under WiFi
ISR jitter — chip phase is anchored to chip-rate time, not edge time, so a
single jittered edge can no longer permanently desync alignment.

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

- **Fireplace ignores packets** → 90 % of the time this is the wrong serial
  or wrong ECC `(c, d)`. On the T-Embed, just run on-device learn-mode
  (long-press encoder, point at OEM remote, confirm). On any board, the
  HA `Pair Remote` / `Confirm Pairing` buttons do the same.
- **Pairing never converges** → confirm `gdo0_pin` is wired (plain ESP32
  builds RX-disabled by default), and that the OEM remote is held within
  ~30 cm of the ESP32. Check the logs for `decode: chips=… pkts=…` lines —
  `pkts > 0` means RX is working; if you see only `chips=…` without `pkts`,
  there's a signal-quality issue.
- **TX stalls / fireplace stops responding to commands after a while** →
  a long loop iteration (display redraw, WiFi housekeeping, learn-mode
  service) can underflow the CC1101's TX FIFO, leaving the chip stuck.
  The component now auto-recovers on the next user input; if you ever
  see persistent silence, a power-cycle is the hard reset.
- **Compile fails** → ESPHome ≥ 2024.7 needed for the climate fan-mode work;
  the climate entity also requires the ESP-IDF framework (Arduino is
  untested here).
- **Light won't turn on** → by design, only when `power` is on. You can
  pre-dial a level while power is off, though — it'll apply on the next
  power-on.
- **WS2812 strip shows blue when expecting orange/red** → check
  `rgb_order:` in YAML. ESPHome's `chipset: WS2812` driver expects native
  GRB order on the wire, even though the LilyGo example claims GBR (that
  refers to FastLED's internal ordering, not the chip's wire format).
- **HomeKit fan slider snaps from 0 % to LOW** → known limitation of HA's
  HomeKit bridge; it converts the slider via `percentage_to_ordered_list_item`
  with `[LOW, MIDDLE, MEDIUM, HIGH]` and has no off step.

## Safety

⚠️ **This controls a gas appliance.** Always keep the original remote
available, install CO detectors, follow local codes, and consider timeout
automations. Test thoroughly before relying on it.

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
- **[j2deen/proflame2-esp](https://github.com/j2deen/proflame2-esp)** and
  the parent **[marksieczkowski/proflame2-esp](https://github.com/marksieczkowski/proflame2-esp)**
  — the original ESPHome external-component skeleton, ESP-IDF framework
  setup, secondary-flame support, and non-blocking TX state machine. This
  project began as a fork of j2deen's work; it has since been detached
  from the fork network as it diverged substantially (ESP32-S3 / T-Embed
  target, RX path, on-device learn-mode, climate, light, state restoration,
  device UI, LED strip, web UI).
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

## License

MIT — see [LICENSE](./LICENSE).

## Disclaimer

Not affiliated with SIT Group, ProFlame, or any fireplace manufacturer. Use
at your own risk.
