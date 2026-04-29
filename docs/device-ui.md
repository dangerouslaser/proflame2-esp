# Device UI (T-Embed CC1101)

[← Back to README](../README.md)

The T-Embed build ships with a self-contained LCD + rotary encoder UI that
operates the fireplace independently of HA / WiFi. Long-press the encoder
to enter learn-mode pairing; rotate to navigate; click to toggle or edit.

## Idle screen

Shows the current state of every controllable field plus a status bar
(HA connectivity, battery, signal). The selection cursor highlights one
field at a time.

## Encoder gestures

- **Rotate** in *navigate* mode — moves the cursor through fields in
  order: FLAME → FAN → LIGHT → SEC FLAME → POWER → LEDs → INFO.
- **Click** a binary field (POWER, SEC FLAME, LEDs) — toggles it.
- **Click** a numeric field (FLAME, FAN, LIGHT) — enters *edit* mode.
- **Rotate** in *edit* mode — adjusts the field's value (clamped 0–6).
- **Click** to exit *edit* mode back to *navigate*.
- **Click** INFO — opens the info screen (serial, ECC, signal strength,
  IP, uptime). Any rotation dismisses it.
- **Long-press encoder** — starts on-device pairing. See
  [pairing-tembed.md](pairing-tembed.md) for the flow.

## Pre-dial while off

`LIGHT` and `SEC FLAME` stay editable even when power is off. The values are
stashed and applied on the next power-on, so you can dial in your preferred
setup before lighting the fireplace.

## Settings cog (LEDs)

Toggles the bottom-edge WS2812 fire animation. Same state as the HA
`Status LEDs Enabled` switch — flipping one flips the other.

## Pair button

The T-Embed's dedicated user button (GPIO 6) — long-press to start
learn-mode. Same effect as a long-press of the encoder button. Useful if
you've navigated deep into the menu and don't want to back out.

## Backlight

Dims off after 30 s of no input; the next encoder turn or button press wakes
it instantly. Configurable via the `backlight:` field on the `proflame2.ui`
block — point it at any `light::LightState` and the UI will own its
dim-on-idle / wake-on-interaction behavior.

## WS2812 fire effect

Runs whenever the fireplace power switch is on AND the master `Status LEDs
Enabled` switch is on. The animation is an `addressable_lambda` that
mixes red / orange / yellow at roughly 5:4:3 across the 8 pixels with an
80 ms update interval — fast enough to feel alive, slow enough to keep RMT
load low. Disable it from HA or via the device's settings cog.

The strip uses the chip's native GRB byte order. The LilyGo example
documents `rgb_order: GBR` but that refers to FastLED's *internal*
ordering, not the chip's wire format — ESPHome's `chipset: WS2812` driver
expects GRB on the wire.
