# Home Assistant entities

[← Back to README](../README.md)

After flashing, expect (entities you didn't enable in YAML are simply not
exposed):

## Manual controls

| Entity | Type | Notes |
|---|---|---|
| `switch.fireplace_power` | switch | manual on/off |
| `number.fireplace_flame_height` | number | 0–6 |
| `number.fireplace_fan_speed` | number | 0–6 |
| `light.fireplace_light` | light | brightness 1–100 % → level 1–6, gated on power |
| `switch.fireplace_secondary_flame` | switch | secondary burner |
| `switch.fireplace_aux_power` | switch | aux outlet |
| `switch.fireplace_pilot_mode` | switch | IPI/CPI |

## Climate (optional)

| Entity | Type | Notes |
|---|---|---|
| `climate.fireplace` | climate | HEAT/OFF thermostat with fan modes — see [climate.md](climate.md) |
| `number.fireplace_heat_flame_level` | number (config) | 1–6, used at burner-on |
| `number.fireplace_heat_fan_level` | number (config) | 0–6, used at burner-on |
| `number.fireplace_heat_light_level` | number (config) | 0–6, used at burner-on (default 0 = off) |
| `switch.fireplace_heat_secondary_flame` | switch (config) | applied at burner-on |

## Pairing (when learn-mode is wired up)

| Entity | Type | Notes |
|---|---|---|
| `button.fireplace_pair_remote` | button | start a 60 s capture window |
| `button.fireplace_confirm_pairing` | button | commit captured serial+ECC to NVS |
| `button.fireplace_cancel_pairing` | button | abort the capture |

## Diagnostic text-sensors

| Entity | Type | Notes |
|---|---|---|
| `sensor.fireplace_serial_number` | text (diagnostic) | e.g. `0x320A02` |
| `sensor.fireplace_ecc_constants` | text (diagnostic) | `c1=0x8 d1=0xE c2=0xB d2=0x7` |
| `sensor.fireplace_pairing_source` | text (diagnostic) | `NVS (learned)` or `YAML` |

## T-Embed-only

All of the device-UI toggles below are backed by HA template entities so HA
and the on-device settings page share one source of truth.

| Entity | Type | Notes |
|---|---|---|
| `light.proflame2_tembed_status_leds` | light | manual control of the bottom-edge WS2812 strip + effects |
| `switch.proflame2_tembed_status_leds_enabled` | switch (config) | master enable for the auto fire animation |
| `switch.proflame2_tembed_battery_as_bar` | switch (config) | ON = graphical battery bar; OFF = `BAT NN%` text. Default ON. |
| `switch.proflame2_tembed_clock_on_idle` | switch (config) | ON = LCD shows large clock when idle past timeout; OFF = backlight off. Default OFF. |
| `switch.proflame2_tembed_invert_encoder_direction` | switch (config) | Software-flips encoder rotation. Default OFF. |
| `select.proflame2_tembed_backlight_timeout` | select (config) | `"15s"` / `"30s"` / `"1m"` / `"5m"` / `"never"`. Default 30 s. |
| `sensor.proflame2_tembed_battery` | sensor (diagnostic) | BQ27220 fuel gauge state-of-charge |

The HA entity IDs above use the example-YAML names (`fireplace`,
`proflame2_tembed`); yours will follow whatever `name:` you set in each
YAML's top-level `esphome:` block.

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

Once you change any of these, the latest value is what gets restored on the
next power-on (per-field — see "State restoration" below).

## State restoration

The light's *level* and the secondary-flame's on/off are remembered across
power cycles. If you had the light at 4, power off, then power on, it comes
back at 4 — the HA entity, the device LCD, and the RF state all agree. You
can also pre-dial a level via either surface while power is off; it'll
apply on the next power-on.

The same restoration applies to **Secondary Flame**: toggling the fireplace
off doesn't lose your "secondary on/off" preference. You can pre-dial it OFF
or ON while power is off, and the next power-on respects what you picked.

## Light entity gating

The fireplace's light only physically operates while the burner is running.
The HA `light` entity reflects that reality — it goes off whenever the
fireplace powers down and refuses an "on" attempt while power is off (with
a warning in the log). You can pre-dial a level via either surface while
power is off; it'll apply on the next power-on.
