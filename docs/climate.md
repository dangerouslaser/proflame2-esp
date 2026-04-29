# Climate (thermostat)

[← Back to README](../README.md)

Optional but recommended. Adds a real `climate` entity to Home Assistant
(HEAT/OFF thermostat) so the HomeKit bridge can expose the fireplace as a
Thermostat / HeaterCooler accessory with the room temperature and a target
setpoint — instead of asking your phone for the right "input number" for
flame height.

## Configuration

The climate entity needs a current-temperature source from HA. Use a template
helper for indirection so you can repoint the source without reflashing:

In Home Assistant: **Settings → Devices & Services → Helpers → Create Helper
→ Template a sensor**. Set the state template to e.g.
`{{ states('sensor.my_ecobee_temperature') }}` and unit `°F` (or whatever
your sensor reports).

Then in your ESPHome YAML:

```yaml
sensor:
  - platform: homeassistant
    id: room_temp
    entity_id: sensor.fireplace_room_temperature   # the helper above
    filters:
      - lambda: 'return (x - 32.0f) * 5.0f / 9.0f;'  # °F → °C (climate is °C internally)

proflame2:
  # ... entities from installation.md ...
  heat_flame_level:
    name: "Heat Flame Level"
    icon: "mdi:fire"
    entity_category: config
  heat_fan_level:
    name: "Heat Fan Level"
    icon: "mdi:fan"
    entity_category: config
  heat_light_level:
    name: "Heat Light Level"
    icon: "mdi:lightbulb"
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

The example YAMLs ship with this config already wired up via
[`proflame2_common.yaml`](../proflame2_common.yaml).

## How the climate behaves

Intentionally simple bang-bang thermostat:

- **HEAT mode**: when current < target − hysteresis (default 0.5 °C), the
  climate sends a single packet that turns power on, sets `flame_level`,
  `fan_level`, `light_level`, and `secondary_flame` to the values held in the
  four `heat_*` config entities, and queues a transmit.
- When current ≥ target + hysteresis, the climate sends a packet that turns
  power off and zeroes fan + secondary flame so the fireplace doesn't sit
  blowing room-temperature air.
- The heat-mode config values are applied **on every burner-on cycle**, not
  just the first one. To change behavior persistently, change the config
  entities — the next cycle picks up the new values. T-Embed users can also
  edit the same four entities directly from the on-device climate editor —
  see [device-ui.md](device-ui.md#climate-editor).
- **Hysteresis tick** is 5 s (climate `loop()` won't re-evaluate more often
  than that).
- **OFF mode**: the climate also drops fan + secondary along with power, so
  switching the climate to OFF is a clean stop.

## Fan modes (HomeKit-friendly)

The climate exposes built-in fan modes (OFF / LOW / MEDIUM / HIGH → levels
0 / 2 / 4 / 6). HA's HomeKit bridge requires built-in modes (not custom
strings) to surface a RotationSpeed slider on the climate accessory; the
existing `fan` Number entity is still available for fine-grained 0–6 control.
