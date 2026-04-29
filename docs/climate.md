# Climate (thermostat)

[← Back to README](../README.md)

Optional but recommended. Adds a real `climate` entity to Home Assistant
(HEAT/OFF thermostat) so the HomeKit bridge can expose the fireplace as a
Thermostat / HeaterCooler accessory with the room temperature and a target
setpoint — instead of asking your phone for the right "input number" for
flame height.

## Configuration

The climate entity needs a current-temperature source from HA. **The example
YAMLs already wire this up via `proflame2_common.yaml` — but they reference
a specific helper that you have to create yourself, named exactly as below,
or the climate boots into "Temperature unavailable" and stays off.**

### 1. Create the HA Template helper

In Home Assistant: **Settings → Devices & Services → Helpers → Create
Helper → Template a sensor**. Fill in:

- **Name**: `Fireplace Room Temperature` *(this exact name — HA slugifies it
  to `sensor.fireplace_room_temperature`, which is what the YAML references.
  If you pick a different name, edit the `entity_id:` in step 2 to match.)*
- **State template**: `{{ states('sensor.YOUR_THERMOMETER') }}` —
  point at whatever HA-side sensor reports your living-room temperature.
  Examples: `sensor.living_room_temperature`, an Ecobee remote-sensor entity,
  a Z-Wave multisensor, etc.
- **Unit of measurement**: match your source sensor — `°F` if it reports
  Fahrenheit, `°C` if it reports Celsius.
- **Device class**: `Temperature`

Why a helper rather than referencing the source sensor directly? Indirection.
You can swap thermometers without reflashing the device — just change what
the helper points at.

### 2. The ESPHome YAML

Already in the example YAMLs; reproduced here so you know what to tweak if
you used a different helper name or your source sensor is in Celsius.

```yaml
sensor:
  - platform: homeassistant
    id: room_temp
    entity_id: sensor.fireplace_room_temperature   # ← the helper from step 1
    # If your helper reports °F, keep this lambda (climate is °C internally).
    # If your helper reports °C already, DELETE the filters: block entirely.
    filters:
      - lambda: 'return (x - 32.0f) * 5.0f / 9.0f;'  # °F → °C

# (the proflame2: block lives in proflame2_common.yaml — entities below are
# pulled in automatically when you `packages: !include` it)
proflame2:
  heat_flame_level:    { name: "Heat Flame Level",     icon: "mdi:fire",      entity_category: config }
  heat_fan_level:      { name: "Heat Fan Level",       icon: "mdi:fan",       entity_category: config }
  heat_light_level:    { name: "Heat Light Level",     icon: "mdi:lightbulb", entity_category: config }
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

### 3. What happens if the helper is missing or broken

The climate fails safe. If the helper doesn't exist, returns `unavailable`
or `unknown`, or HA disconnects entirely, `current_temperature` becomes NaN
and the climate's hysteresis loop:

- Drops the burner if it's currently on (so the fireplace doesn't run open-
  loop without a temperature reference).
- Logs `Temperature unavailable; shutting fireplace down` at WARN.
- Stays IDLE until a valid reading returns.

So a misnamed helper is a "climate stays cold" symptom, not a "climate runs
forever" symptom. Check the device logs (`logger:` is enabled by default
in both example YAMLs) for the warning if HEAT mode looks dead.

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
