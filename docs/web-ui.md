# Standalone web UI

[← Back to README](../README.md)

Every build configured with `web_server:` exposes the ESPHome dashboard at
`http://<device-name>.local/` (or via the device's IP). It mirrors every
entity the device exposes — POWER, FLAME, FAN, LIGHT, SEC FLAME, LEDs,
climate, and the pair / confirm / cancel buttons — with toggles, sliders,
and a brightness slider for the light. **No Home Assistant required**;
it's a fully usable manual fallback when HA is offline or when you don't
run HA at all.

The example YAMLs ship with HTTP basic auth enabled — set the password via
the `fireplace_remote_web_password` secret. Strip the `auth:` block if
you'd rather not require credentials on a trusted LAN.

## When this is useful

- HA add-on hosting your dashboard goes down — the device still works.
- You don't run HA, just want a local web page.
- You're testing OTA pushes without bringing HA up first.
- You want to give a guest a temporary URL without giving them HA access.

## What it isn't

- It's not a replacement for the on-device UI on the T-Embed — touch
  hardware always wins for "the lights are out and HA crashed."
- It doesn't include the climate's HEAT/OFF mode picker on every ESPHome
  version (depends on web_server v3 features); HA is the better surface
  for thermostat control.
