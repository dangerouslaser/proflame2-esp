# On-device pairing (plain ESP32 + CC1101)

[← Back to README](../README.md) · [← Pairing overview](pairing.md)

Same algebraic recovery as the T-Embed flow — the ESP32 listens to your
existing OEM remote and infers the serial + ECC constants — but the
confirm step happens via a Home Assistant button instead of an
on-device encoder. This requires `gdo0_pin` to be wired up; without it,
RX is unavailable and you're stuck with the [SDR flow](pairing-sdr.md).

## Steps

1. Flash [`proflame2_fireplace.yaml`](../proflame2_fireplace.yaml) with
   placeholder `serial_number` / `ecc_constants`. Make sure `gdo0_pin` is
   set in YAML and wired to the CC1101's GDO0 pin.
2. In Home Assistant, find the three pairing buttons exposed by the device:
   **Pair Remote**, **Confirm Pairing**, **Cancel Pairing**.
3. Click **Pair Remote**. The device starts a 60 s listening window;
   ESPHome logs say `Learn-mode armed — press any button on the OEM remote`.
4. Within ~30 cm of the device, press buttons that change **both halves of
   the packet**:
   - At least one button that changes `cmd1`: power, light, pilot, or
     thermostat.
   - At least one button that changes `cmd2`: flame up/down, fan,
     secondary flame, or aux.

   Logs report `N packets agree (distinct cmd1=N/2, cmd2=N/2)` — both
   distinct counts need to reach 2 before convergence. Watch for the
   per-axis hint at the end of each line.

   *Why both?* The OEM remote sends ~5 byte-identical repeats per press,
   so packet count alone is tautological. Diversity on both `cmd1` and
   `cmd2` is what validates the inversion formula against varying input;
   without it, pairing can converge on garbage values that won't control
   the fireplace. See [Safety gates](pairing.md#safety-gates-any-flow).
5. Once the log shows `CONVERGED — awaiting user confirm: serial=0x…`, click
   **Confirm Pairing**. The captured serial + ECC are committed to NVS, and
   the device logs `Learned values committed`.
6. Click **Cancel Pairing** at any point to abort the listening window
   without committing.
7. **Pull the batteries from your OEM remote** —
   see [After pairing](pairing.md#-after-pairing-pull-the-batteries-from-your-oem-remote).

## Safety gates

The same gates as the T-Embed flow apply — see
[Safety gates](pairing.md#safety-gates-any-flow) for the full list,
including the cmd-byte diversity requirement and the `ECC formula
mismatch` bail. The only difference is the *confirm* step: T-Embed
requires a physical encoder long-press, plain ESP32 trusts the HA button
click.

If you'd rather not expose the `pair_confirm` button in HA, you can omit it
from your YAML and instead write a YAML automation that calls
`learn_confirm()` after some other trigger (e.g. a separate physical GPIO
button).

## After pairing

→ [Pull the batteries from your OEM remote](pairing.md#-after-pairing-pull-the-batteries-from-your-oem-remote).
