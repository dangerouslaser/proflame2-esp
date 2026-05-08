# Protocol details

[← Back to README](../README.md)

For contributors and curious users. Day-to-day operation doesn't require
any of this — the component handles everything internally.

## Wire-level

| | |
|---|---|
| **Frequency** | 314.973 MHz (some variants use 315 MHz or 433 MHz) |
| **Modulation** | OOK (On-Off Keying) |
| **Baud rate** | 2400 |
| **Encoding** | Thomas Manchester |
| **Packet** | 7 words × 13 bits = 91 bits per frame, 5 repeats per command, ≥200 ms between commands |
| **Word layout** | sync(1) + start guard(1) + data(8) + pad(1) + parity(1) + end guard(1) + 1 |

## Word order

| word | content | source |
|---|---|---|
| 0 | `serial1`  (high byte of serial number) | YAML `serial_number` (or NVS) |
| 1 | `serial2`  (mid byte) | YAML `serial_number` (or NVS) |
| 2 | `serial3`  (low byte) | YAML `serial_number` (or NVS) |
| 3 | `cmd1`     (pilot/light/thermostat/power) | live state |
| 4 | `cmd2`     (secondary/fan/aux/flame) | live state |
| 5 | `err1`     | computed from `cmd1` + `(c1, d1)` |
| 6 | `err2`     | computed from `cmd2` + `(c2, d2)` |

`err1` / `err2` are the **remote-specific** part of the packet — that's
where the pairing identity is encoded. They can be inverted from `cmd*` and
`(c, d)`, which is what makes the on-device pairing flow possible.

## ECC math

For each `(cmd_byte, c, d)` triple:

```
hi = cmd_byte >> 4
lo = cmd_byte & 0xF

X = (c ^ (hi<<1) ^ hi ^ (lo<<1)) & 0xF
Y = (d ^ hi ^ lo) & 0xF
err = (X << 4) | Y
```

The inverse (used by learn-mode) given `(cmd_byte, err_byte)`:

```
hi = cmd_byte >> 4
lo = cmd_byte & 0xF
X = err_byte >> 4
Y = err_byte & 0xF

c = X ^ (hi<<1) ^ hi ^ (lo<<1)    # mask to & 0xF
d = Y ^ hi ^ lo                    # mask to & 0xF
```

A frame with `cmd_byte = 0x00` makes the equation degenerate to
`err_byte = (c << 4) | d` — so a zero-cmd capture gives `(c, d)` directly.

## RX architecture (learn-mode)

The receive path mirrors `rtl_433_ESP`'s strategy: the CC1101 is configured
at DATARATE 17.24 kbaud and 270 kHz RX bandwidth — well above the 2400-baud
chip rate — so it acts as a raw OOK envelope detector rather than a
clock-recovery demodulator. GDO0 fires an ISR on every envelope edge; edge
timestamps land in a 1024-entry SPSC ring; on the inter-burst silence
(>6 ms) we round each pulse-width to chip-count steps and pack it into a
chip-rate-locked bit buffer. A linear scan of the buffer looks for the
4-chip `1110` sync, Manchester-decodes 11 bits per word, and validates the
7-word ProFlame frame.

This is fundamentally more robust than per-edge streaming decode under WiFi
ISR jitter — chip phase is anchored to chip-rate time, not edge time, so a
single jittered edge can no longer permanently desync alignment.

The SPSC ring's head/tail indices are `std::atomic<size_t>` with explicit
acquire/release ordering; on the dual-core ESP32-S3 (T-Embed), the ISR may
run on a different core than the loop, so plain `volatile` would be UB
under the C++ memory model.

See [`components/proflame2/proflame2_decode.cpp`](../components/proflame2/proflame2_decode.cpp)
for the host-testable decoder logic and
[`components/proflame2/proflame2_rx.cpp`](../components/proflame2/proflame2_rx.cpp)
for the ISR + ring buffer.

## Convergence gate (learn-mode)

Once decoded packets reach `on_packet_decoded_` in
[`proflame2_learn.cpp`](../components/proflame2/proflame2_learn.cpp), the
candidate must clear three checks before the state machine moves to
`kConverged` and presents the values for user confirmation:

1. **`valid_packet_count ≥ 3`** — same baseline that's always been there.
2. **`cmd1.distinct ≥ 2` AND `cmd2.distinct ≥ 2`** — at least two
   different `cmd1` bytes and two different `cmd2` bytes must have all
   inverted to the same `(c, d)`. The OEM remote sends ~5 byte-identical
   repeats per button press, so packet count alone proves nothing — only
   `cmd_byte` diversity validates that the inversion formula fits this
   remote.
3. **No `(c, d)` drift across distinct `cmd_byte`s** — if a *new* `cmd1`
   or `cmd2` value inverts to a *different* `(c, d)` than the running
   candidate, the state machine bails to `kFailed` with an `ECC formula
   mismatch` log line. That's the smoking gun for a non-standard ProFlame
   variant where the formula in [ECC math](#ecc-math) doesn't apply.

Drift on a *known* `(cmd1, cmd2)` pair (i.e. same input bytes producing
different `(c, d)` between two packets) is algebraically impossible from
the formula, so it gets logged as decoder noise (parity-passed bit shift
etc.) and ignored without disturbing the candidate.

The `CmdHistory` struct in
[`proflame2_cc1101.h`](../components/proflame2/proflame2_cc1101.h) holds
the per-axis seen-set: 8-element bounded array, append-if-absent on
matching packets.

## Where transmit comes from

[`components/proflame2/proflame2_cc1101.cpp`](../components/proflame2/proflame2_cc1101.cpp)
holds the TX state machine. `build_packet()` assembles the 91-bit payload from
`current_state_` + `serial_number_` + `(c1, d1, c2, d2)`; `encode_manchester()`
applies Thomas-Manchester encoding into the FIFO; `start_tx_()` strobes the
CC1101 into TX mode and refills the FIFO from the loop until the 5-repeat
burst completes.
