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
4. Press any button on your OEM remote (power, flame up, etc.) 3 times within
   ~30 cm of the device. Logs report `N/3 valid packets agree`.
5. Once the log shows `CONVERGED — awaiting user confirm: serial=0x…`, click
   **Confirm Pairing**. The captured serial + ECC are committed to NVS, and
   the device logs `Learned values committed`.
6. Click **Cancel Pairing** at any point to abort the listening window
   without committing.
7. **Pull the batteries from your OEM remote** —
   see [After pairing](pairing.md#-after-pairing-pull-the-batteries-from-your-oem-remote).

## Safety gates

The same gates as the T-Embed flow apply — packets must agree byte-for-byte,
checksum cross-validates, etc. The only difference is the *confirm* step:
T-Embed requires a physical encoder long-press, plain ESP32 trusts the HA
button click.

If you'd rather not expose the `pair_confirm` button in HA, you can omit it
from your YAML and instead write a YAML automation that calls
`learn_confirm()` after some other trigger (e.g. a separate physical GPIO
button).

## After pairing

→ [Pull the batteries from your OEM remote](pairing.md#-after-pairing-pull-the-batteries-from-your-oem-remote).
