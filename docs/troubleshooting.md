# Troubleshooting

[← Back to README](../README.md)

- **Fireplace ignores packets** → 90 % of the time this is the wrong serial
  or wrong ECC `(c, d)`. On the T-Embed, just run on-device learn-mode
  (long-press encoder, point at OEM remote, confirm). On any board, the
  HA `Pair Remote` / `Confirm Pairing` buttons do the same. See
  [pairing.md](pairing.md).
- **Settings revert after I change them / fireplace state desyncs from
  HA** → if you cloned the OEM remote (anything that isn't option 3 in
  [Capture options](pairing-sdr.md#capture-options)), both devices share
  the same identity and the receiver honors whichever speaks last. Pull
  the batteries from the OEM remote — see
  [After pairing](pairing.md#-after-pairing-pull-the-batteries-from-your-oem-remote).
- **Fireplace turns itself OFF (or back ON, or to a different setting)
  ~5–10 minutes after I changed it from HA, with no one touching the
  OEM remote** → same root cause as the previous bullet, but the
  symptom is delayed-not-instant because the OEM remote autonomously
  re-broadcasts its full state about every 9.5 minutes even with no
  button press. Whatever the OEM remote thinks the fireplace should
  be doing, it will eventually reassert. Pulling the batteries silences
  the autonomous heartbeat — see
  [Why pulling batteries matters](pairing.md#why-pulling-batteries-matters-its-not-just-stray-button-presses)
  for the mechanism and source citations.
- **Pairing never converges** → confirm `gdo0_pin` is wired (plain ESP32
  builds RX-disabled by default), and that the OEM remote is held within
  ~30 cm of the ESP32. Check the logs for `decode: chips=… pkts=…` lines —
  `pkts > 0` means RX is working; if you see only `chips=…` without `pkts`,
  there's a signal-quality issue. If you see `packets agree` lines but the
  `distinct cmd1=N` or `cmd2=N` counter is stuck at 1, you're only pressing
  buttons that affect one half of the packet — press something that changes
  the other half (POWER/LIGHT/PILOT change `cmd1`; FLAME/FAN/SEC change
  `cmd2`).
- **Pairing fails immediately with `ECC formula mismatch` in the log** →
  the on-device learn flow inverts each packet's checksum to recover the
  per-remote `(c, d)` constants, then validates that *different* button
  presses (different `cmd_byte`s) all invert to the same `(c, d)`. If they
  don't, the standard ProFlame inversion formula doesn't fit your remote —
  most likely a protocol variant with different word order or XOR pattern.
  Workaround: capture two distinct button presses with rtl_433 / OMG and
  open an issue with the raw cmd/chk bytes — see the
  [SDR pairing flow](pairing-sdr.md). The error log line includes both
  presses' full data so you can reproduce the analysis offline.
- **TX stalls / fireplace stops responding to commands after a while** →
  a long loop iteration (display redraw, WiFi housekeeping, learn-mode
  service) can underflow the CC1101's TX FIFO. The component
  auto-recovers and now re-queues the dropped packet on the next loop
  tick (see commit history); if you ever see persistent silence, a
  power-cycle is the hard reset.
- **Compile fails** → ESPHome ≥ 2024.7 needed for the climate fan-mode work;
  the climate entity also requires the ESP-IDF framework (Arduino is
  untested here).
- **Climate stays cold / `Temperature unavailable; shutting fireplace down`
  in logs** → the example YAMLs reference a HA template helper named
  `sensor.fireplace_room_temperature` for the room temperature feed. If you
  haven't created it (or named yours differently), the climate has nothing
  to read and fails safe by dropping the burner. See
  [climate.md → Configuration](climate.md#configuration) for the helper
  setup.
- **Light won't turn on** → by design, only when `power` is on. You can
  pre-dial a level while power is off, though — it'll apply on the next
  power-on. See [entities.md → State restoration](entities.md#state-restoration).
- **WS2812 strip shows blue when expecting orange/red** → check `rgb_order:`
  in YAML. ESPHome's `chipset: WS2812` driver expects native GRB order on
  the wire, even though the LilyGo example claims GBR (that refers to
  FastLED's internal ordering, not the chip's wire format).
- **HomeKit fan slider snaps from 0 % to LOW** → known limitation of HA's
  HomeKit bridge; it converts the slider via `percentage_to_ordered_list_item`
  with `[LOW, MIDDLE, MEDIUM, HIGH]` and has no off step.
- **HA shows the device as "unavailable" with `EncryptionHelloAPIError` in
  logs** → the ESPHome dashboard, if running 24/7 next to HA, can leak TCP
  sockets on each handshake retry and eventually exhaust the device's
  `max_connections` cap. Stop the dashboard container when not actively
  flashing/editing — the HA integration doesn't need it.
