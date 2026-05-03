# Pairing your remote

[← Back to README](../README.md)

The fireplace receiver only listens to packets that match its paired remote's
24-bit **serial number** plus a remote-specific set of four 4-bit **ECC
constants** (`c1, d1, c2, d2`). To control the fireplace from this component
you need either to recover those values from your existing OEM remote ("clone"
the remote) or to teach the fireplace receiver to trust the ESP32 as a *new*
remote ("replace" the OEM remote).

## Pick a flow

| Flow | Hardware required | Effort | Result |
|---|---|---|---|
| **[On-device learn-mode (T-Embed)](pairing-tembed.md)** | T-Embed CC1101 | 30 s, no SDR | Clone of OEM remote |
| **[On-device learn-mode (plain ESP32)](pairing-esp32.md)** | ESP32 + CC1101 + `gdo0_pin` wired | 1 min from HA | Clone of OEM remote |
| **[SDR capture](pairing-sdr.md)** option 1/2 | RTL-SDR or another OMG/rtl_433 ESP | 5–10 min | Clone of OEM remote |
| **[Pair as a new remote](pairing-sdr.md#capture-options)** option 3 | Access to fireplace's pairing button | one-time | OEM remote *replaced* |

If you have a T-Embed, just use the on-device learn-mode — it's why this
project exists. The other paths are for the generic-ESP32 build or for
recovery scenarios.

## ⚠️ After pairing: pull the batteries from your OEM remote

Cloning paths (everything except option 3) leave the OEM remote and the ESP32
sharing the same 24-bit serial and the same ECC constants. The fireplace
receiver treats packets from either as equally authoritative — it has no way
to tell them apart.

If the OEM remote stays powered, **its packets will override the ESP32**.
Symptoms:

- HA shows the burner ON but it's actually OFF (or vice versa).
- Settings appear to revert seconds after you change them in HA.
- Climate-driven heat cycles get clobbered by stray remote presses.
- A bump to the remote in a drawer triggers a real fireplace command.

**Fix: pull the batteries from the OEM remote.** Keep them somewhere safe —
you may want the remote later for fallback control or to re-clone if NVS gets
wiped.

### Why pulling batteries matters (it's not just stray button presses)

The OEM remote does not require a button press to transmit. It is
**stateful and autonomous**: it holds its own model of "what the fireplace
should be doing" and *re-broadcasts that full state on a timer*, even when
nothing has touched it. So if your OEM remote thinks `POWER=OFF, flame=0`
and your ESP turns the burner on, the OEM remote's next periodic broadcast
will turn it right back off — no handshake, no confirmation, just a
unilateral re-assertion that the IFC accepts because the serial matches.

Two independent community observations on the timing:

- *"the Proflame remote… has an 11 bit state and sends all 11 bits every
  time. It does this both when it thinks the user is done [post-press
  settle] and periodically."* — [Bond Home forum thread][bond]
- *"The remote proper sends continual updates of it's current state
  roughly every 9.5 minutes or so."* — [Home Assistant community thread][ha]

So expect symptoms to surface several minutes after an ESP-driven change —
not seconds — which is what makes this so confusing to debug otherwise.
Pulling the batteries silences the autonomous heartbeat completely.

There is no two-way protocol you can lean on to detect or counter this.
The IFC does not transmit back — we verified this empirically by listening
on 314.973 MHz for ~1.5 s after every TX across many commands and got
zero packets in return. The OEM remote isn't "checking in" with the
fireplace; it's just talking to itself on a timer and the IFC happens
to be listening.

[bond]: https://forum.bondhome.io/t/proflame-2-fireplace-remote/2156
[ha]: https://community.home-assistant.io/t/proflame-remote/520529

### Option 3 sidesteps all of this

This does **not** apply to option 3 (*"pair as a new remote"*). That flow
puts the fireplace receiver into pairing mode and *replaces* the OEM remote's
identity with the ESP32's; the OEM remote stops working entirely, so there's
nothing left to fight the ESP32. If you want a single-source setup with no
battery-pulling required, option 3 is the path to take — trade-off is a
one-way trip until you re-pair the OEM remote.

## Safety gates (any flow)

This is a gas appliance, so the on-device learn-mode applies multiple checks
before letting you commit:

- 3+ valid packets within a single 60 s capture window.
- All packets agree on serial AND all four ECC constants byte-for-byte.
- Each packet's checksum cross-validates against the inferred `(c, d)`.
- Confirmation is a hold (T-Embed) or an explicit HA button click (plain
  ESP32) — no auto-commit on first capture.

If any gate fails, the device discards everything and re-listens.

## Where the values live after pairing

A successful pair writes a versioned, CRC-protected blob to the device's
NVS partition. On every subsequent boot, the component checks that blob
first; valid → use it (overrides whatever's in YAML). Invalid / missing →
fall back to the YAML values.

`dump_config()` reports the source: `Serial Number: 0x… (NVS v1 (learned))`
or `Serial Number: 0x… (YAML)`.

The same info is exposed as three diagnostic entities in HA: `Serial
Number`, `ECC Constants`, `Pairing Source`.
