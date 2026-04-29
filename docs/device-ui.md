# Device UI (T-Embed CC1101)

[← Back to README](../README.md)

The T-Embed build ships with a self-contained LCD + rotary encoder UI that
operates the fireplace independently of HA / Wi-Fi. Three top-level views —
**idle** (the field list), **settings** (a 9-item config menu), and **climate**
(an 8-row thermostat editor) — are all reachable with the encoder.

## Idle view

Cycles through the controllable fields, plus two "menu" entries that open
sub-views:

```
FLAME  4
FAN    2
LIGHT  3      ← active row (font_medium, accent color)
SEC    ON
POWER  ON
CLIMATE >>    ← opens the climate editor
SETTINGS >>   ← opens the settings page
```

The selection cursor highlights one row at a time. The status bar at the top
shows HA connectivity (`HA✓` / `HA?`), Wi-Fi RSSI, and battery (graphical bar
or `BAT NN%` text — see *Settings → Battery as Bar*).

## Encoder gestures

- **Rotate** in *navigate* mode — moves the cursor through fields.
- **Click** a binary field (POWER, SEC FLAME) — toggles it.
- **Click** a numeric field (FLAME, FAN, LIGHT) — enters *edit* mode.
- **Rotate** in *edit* mode — adjusts the field's value (clamped 0–6).
- **Click** to exit *edit* mode back to *navigate*.
- **Click** CLIMATE — opens the [climate editor](#climate-editor).
- **Click** SETTINGS — opens the [settings page](#settings-page).
- **Long-press encoder** — starts on-device pairing. See
  [pairing-tembed.md](pairing-tembed.md) for the flow.

## Pre-dial while off

`LIGHT` and `SEC FLAME` stay editable even when power is off. The values are
stashed and applied on the next power-on, so you can dial in your preferred
setup before lighting the fireplace.

## Pair button

The T-Embed's dedicated user button (GPIO 6) — long-press to start
learn-mode. Same effect as a long-press of the encoder button. Useful if
you've navigated deep into a sub-view and don't want to back out.

---

## Settings page

Click `SETTINGS >>` from idle to enter. Nine items, top to bottom:

| Item | What it does |
|---|---|
| **Status LEDs** | Toggles the WS2812 fire animation on the bottom edge. Same state as the HA `Status LEDs Enabled` switch — flipping one flips the other. |
| **Battery as Bar** | ON = graphical battery bar in the status bar; OFF = `BAT NN%` text. Default ON. |
| **Clock on Idle** | When ON and the user has been idle past the backlight timeout, the LCD shows a large `HH:MM` clock instead of going dark. Requires the `time:` source to be wired. Default OFF (saves battery). |
| **Backlight Timeout** | Cycle through `15s` / `30s` / `1m` / `5m` / `never`. Drives both the dim-on-idle threshold and the screensaver activation. Default 30 s. |
| **Invert Encoder** | Software-flips encoder rotation direction. Useful if your physical encoder feels backwards — some T-Embed batches have differing detent polarity. Default OFF. |
| **Info** | Opens a read-only diagnostic screen: serial number, ECC constants, Wi-Fi RSSI, IP address, uptime. Any rotation or click dismisses it. |
| **Clear Pairing** | Wipes the learned serial + ECC from NVS and reboots back to the YAML defaults. Use this if pairing went sideways and you want a clean slate. |
| **Reboot** | Restarts the device. Useful for forcing a state-restore re-read. |
| **Back** | Returns to the idle view. |

Click an item to activate / toggle it. The four toggle items render `ON` / `OFF`
in green / dim; the action items render `>>` in accent color when selected.

Settings changes also surface in HA — every toggle is backed by a template
switch and every select is a template select, so HA and the device share one
source of truth without separate plumbing.

---

## Climate editor

Click `CLIMATE >>` from idle to enter. Eight rows in a vertical list — same
single-click semantics as the main idle view, with edit mode for the four
numeric rows:

| Row | Click behavior | What it edits |
|---|---|---|
| **MODE** | Toggle HEAT ↔ OFF | `climate.fireplace` mode. Inline current room temp on this row (e.g. `OFF 72°F`). |
| **TARGET** | Click → edit; rotate → ±0.5 °C (≈ 1 °F); click → confirm | `climate.fireplace` setpoint. |
| **FAN** | Cycle OFF → LOW → MED → HIGH → OFF | `climate.fireplace` fan mode. Fan modes are HomeKit-friendly built-ins (level 0 / 2 / 4 / 6). |
| **HEAT FLAME** | Click → edit; rotate → ±1 (clamped 1–6); click → confirm | `number.heat_flame_level` — flame applied at burner-on. |
| **HEAT FAN** | Click → edit; rotate → ±1 (clamped 0–6); click → confirm | `number.heat_fan_level` — fan applied at burner-on. |
| **HEAT LIGHT** | Click → edit; rotate → ±1 (clamped 0–6); click → confirm | `number.heat_light_level` — light applied at burner-on. |
| **HEAT SEC** | Toggle ON / OFF | `switch.heat_secondary_flame` — secondary burner applied at burner-on. |
| **Back** | Return to idle | |

The four `HEAT *` rows mirror the HA-side config entities, so a tweak on the
LCD propagates to HA (and vice versa) and the next time the climate auto-
engages the burner, it picks up the new values. See [climate.md](climate.md)
for what the thermostat does with them.

---

## Status bar

Renders at the top of every view (except the clock screensaver):

- **HA indicator** — `HA✓` when the API connection is live, `HA?` when not.
- **Wi-Fi RSSI** — three-bar glyph; falls back to dim when disconnected.
- **Battery** — graphical bar when *Battery as Bar* is ON (default),
  `BAT NN%` text otherwise. Hidden entirely when no battery sensor is wired.

## Backlight & idle

Dims off after the configured *Backlight Timeout* of no input; the next
encoder turn or button press wakes it instantly. Configurable via the
`backlight_timeout_select:` field on the `proflame2.ui` block — point it at
any template select with the strings `"15s"` / `"30s"` / `"1m"` / `"5m"` /
`"never"` and the UI parses them at runtime.

If *Clock on Idle* is ON, the LCD draws a large `HH:MM` clock instead of
going dark. Same idle threshold; the next interaction returns you to the
view you were on.

## WS2812 fire effect

Runs whenever the fireplace power switch is on AND the master *Status LEDs*
toggle is on. The animation is an `addressable_lambda` that mixes red /
orange / yellow at roughly 5:4:3 across the 8 pixels with an 80 ms update
interval — fast enough to feel alive, slow enough to keep RMT load low.
Disable it from HA or via the device's settings page.

The strip uses the chip's native GRB byte order. The LilyGo example
documents `rgb_order: GBR` but that refers to FastLED's *internal*
ordering, not the chip's wire format — ESPHome's `chipset: WS2812` driver
expects GRB on the wire.
