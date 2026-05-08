# On-device pairing (T-Embed CC1101)

[← Back to README](../README.md) · [← Pairing overview](pairing.md)

The T-Embed's standout feature: pair to your fireplace without an SDR, without
rtl_433, without YAML editing, without anything except the OEM remote you
already own. The device listens for packets, recovers the serial and all four
ECC constants algebraically, asks you to confirm, and writes the result to
NVS.

Total time: about 30 seconds.

## Steps

1. Flash [`proflame2_tembed.yaml`](../proflame2_tembed.yaml) with **any**
   placeholder `serial_number` / `ecc_constants` — they're seed defaults that
   will be overridden once pairing succeeds.
2. Boot the device. The LCD shows the normal state screen.
3. **Hold the encoder button for 1.5 s.** The screen switches to "PAIRING —
   Press a button on the OEM remote".
4. With your OEM remote ~30 cm from the device, press buttons that change
   **both halves of the packet**:
   - **At least one cmd1 button**: power, light, pilot, or thermostat.
   - **At least one cmd2 button**: flame up/down, fan, secondary flame, or
     aux.
   The screen displays the captured serial and ECC values, plus a
   `cmd1 N/2  cmd2 N/2` counter. Keep pressing different buttons (or the
   same set again) until both counters reach `2/2`. The headline tells you
   which side still needs a press ("Press POWER / LIGHT / PILOT" or "Press
   FLAME / FAN / SEC").

   *Why both?* Inside one button press the remote sends ~5 byte-identical
   repeats, so "3 packets agree" by itself is tautological. Diversity on
   both `cmd1` and `cmd2` is what actually validates that the inversion
   formula matches your remote — without it, pairing can converge on
   garbage values that won't control the fireplace.
5. The screen switches to "Hold button to confirm" with the candidate values
   displayed in green. **Hold the encoder button** to commit. A short press
   cancels.
6. The screen briefly shows "Saved." and returns to the normal state.
7. **Pull the batteries from your OEM remote** —
   see [After pairing](pairing.md#-after-pairing-pull-the-batteries-from-your-oem-remote).

Done. The pairing is in NVS and survives reboots. `dump_config()` after the
next reboot will report `Serial Number: 0x… (NVS v1 (learned))`.

## What "didn't converge" looks like

If 60 s elapses without enough valid + diverse packets, the screen shows
`Pairing failed — Press to retry`. Press once to dismiss, hold to start over.

Common causes:

- Remote too far away (move within ~30 cm).
- Remote battery low (try a fresh CR2032).
- RF interference (other 315 / 433 MHz devices nearby — try moving them or
  the T-Embed temporarily).
- Only pressed buttons that change one half of the packet — e.g. only
  POWER (changes `cmd1`) without ever pressing FLAME (changes `cmd2`).
  Check the log for `distinct cmd1=N, cmd2=N` — both need to reach 2.
- The remote is sending malformed packets (check rtl_433 to confirm — see the
  [SDR pairing flow](pairing-sdr.md)).

If the screen instead shows the failure quickly with an `ECC formula
mismatch` line in the logs, see
[Pairing fails immediately with "ECC formula mismatch"](troubleshooting.md)
in the troubleshooting guide — your remote may use a non-standard
ProFlame variant.

## Pair button shortcut

The T-Embed has a dedicated user button on GPIO 6, exposed in the
example YAML as `pair_btn`. Long-press it to start learn-mode without
having to navigate the encoder UI — same effect as a long-press of the
encoder button.

## After pairing

→ [Pull the batteries from your OEM remote](pairing.md#-after-pairing-pull-the-batteries-from-your-oem-remote)
to avoid the cloned-remote conflict.
