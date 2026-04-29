# Installation & quickstart

[← Back to README](../README.md)

## 1. Install the external component

```yaml
external_components:
  - source: github://dangerouslaser/proflame2-esp@main
    components: [proflame2]
```

ESPHome will pull the component on first compile. For local development against
an editable checkout, swap to `type: local` with a path bind-mount.

## 2. Pick the right example YAML

Two ready-to-flash configs ship in the repo. They share the entity definitions
via `proflame2_common.yaml` (an ESPHome `packages:` include) and differ only on
pin map, automations, and on-device UI.

| Your hardware | YAML to use | Notes |
|---|---|---|
| **LilyGo T-Embed CC1101** | [`proflame2_tembed.yaml`](../proflame2_tembed.yaml) | All-in-one — adds LCD UI, rotary encoder, battery sensor, pair button. Pin map fixed by the board. |
| Plain ESP32 dev board + CC1101 breakout | [`proflame2_fireplace.yaml`](../proflame2_fireplace.yaml) | TX works out of the box. Wire `gdo0_pin` to enable on-device pairing (RX). |

Each YAML carries a header block reiterating the target hardware and pin
expectations — read those before adapting either to your wiring.

## 3. Set your secrets

Create `secrets.yaml` next to your example YAML with the values referenced by
the example:

```yaml
wifi_ssid: "your-network"
wifi_password: "..."
fireplace_remote_serial_number: "0x000000"   # placeholder; pair to learn
fireplace_remote_api_key: "<base64-32B>"     # ESPHome native API key
fireplace_remote_ota_password: "..."
fireplace_remote_wifi_ap_password: "..."
fireplace_remote_web_password: "..."         # only if you keep web_server auth
```

Generate a fresh native API key with `openssl rand -base64 32`.

## 4. Flash

```
esphome run proflame2_tembed.yaml      # T-Embed
esphome run proflame2_fireplace.yaml   # generic ESP32
```

## 5. Pair

See [Pairing](pairing.md). On the T-Embed this is a 30-second on-device flow;
on the plain ESP32 it's a few clicks in Home Assistant.

## Minimum hand-rolled configuration

If you'd rather build your YAML from scratch, this is the smallest workable
config (no climate, no pairing buttons, no UI):

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
  ecc_constants:                        # see Pairing → Pairing via SDR
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
needed. The climate + heat-config entities and the diagnostic text-sensors are
**additive** and ignored if you leave them out.
