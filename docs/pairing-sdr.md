# SDR pairing (any board)

[← Back to README](../README.md) · [← Pairing overview](pairing.md)

The traditional manual path. Works on every supported board, including
boards where the CC1101 RX is unavailable (no `gdo0_pin` wired) and the
on-device learn-mode can't run. Two pieces of information need to match
what your physical remote was sending, or the fireplace will ignore your
packets.

## 1. Serial number (24 bits)

Bytes 0–2 of the packet. Set in YAML via `serial_number:`. Default is
`0x12345678`, which is fine if you're going to **pair the ESP32 as a new
remote** (option 3 below). Otherwise you need to capture the serial of your
existing remote and set it explicitly.

## 2. ECC pairing constants (4 × 4 bits)

Bytes 5–6 of the packet (`err1`, `err2`) are computed from `cmd1` / `cmd2` via:

```
X = (c ^ (hi<<1) ^ hi ^ (lo<<1)) & 0xF
Y = (d ^ hi ^ lo) & 0xF
err = (X << 4) | Y
```

`(c1, d1)` is used for `err1`; `(c2, d2)` for `err2`. These are pairing-time
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

The example YAMLs ship with placeholder values that **almost certainly won't
work for your fireplace unaltered.**

## Deriving (c, d) from a captured packet

The math is invertible. For any captured frame you have `cmd_byte` (the
input) and `err_byte` (the output):

- Let `hi = cmd_byte >> 4`, `lo = cmd_byte & 0xF`, `X = err_byte >> 4`,
  `Y = err_byte & 0xF`.
- Then `c = X ^ (hi<<1) ^ hi ^ (lo<<1)` (mask to `& 0xF`).
- And `d = Y ^ hi ^ lo` (mask to `& 0xF`).

Special case: a frame with `cmd_byte = 0x00` makes the equation degenerate
to `err_byte = (c << 4) | d` — so a zero-cmd capture gives you `(c, d)`
directly.

## Capture options

1. **Use your existing remote** with [rtl_433](https://github.com/merbanan/rtl_433)
   and an [RTL-SDR](https://www.rtl-sdr.com/buy-rtl-sdr-dvb-t-dongles/):
   ```
   rtl_433 -f 314973000 -R 207
   ```
   The decoder prints serial + cmd1/err1 + cmd2/err2 for every press. Toggle
   different controls (power, fan, light) to capture multiple `(cmd, err)`
   pairs and verify your derived constants.
2. **OpenMQTTGateway** (OMG) on a separate ESP32 also decodes ProFlame 2.
   See its [ProFlame 2 driver](https://docs.openmqttgateway.com/) — it
   embeds rtl_433_ESP and produces the same fields.
3. **Pair as a new remote** (will *replace* your existing remote — only one
   remote can be paired at a time): pick any 24-bit serial and any
   `(c1, d1, c2, d2)`, put the fireplace receiver in pairing mode (see your
   fireplace manual), send a command from the ESP32, and the receiver will
   accept whatever it sees. After this, the OEM remote stops working until
   you re-pair it back.

## After pairing

If you used option 1 or 2, → [Pull the batteries from your OEM remote](pairing.md#-after-pairing-pull-the-batteries-from-your-oem-remote).

Option 3 doesn't have the conflict — the OEM remote stops working as soon as
the receiver accepts the ESP32, so there's nothing to disable.
