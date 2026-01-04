# ProFlame 2 ESPHome Component for ESP32 + CC1101

Control your ProFlame 2 fireplace system using an ESP32 and CC1101 RF module through Home Assistant with native ESPHome integration.

This fork has had multiple updates from the original:
- The file structure was updated to follow [example provided by ESPHome](https://esphome.io/components/external_components/#example-of-git-repositories)
- Framework was moved from Arudino to ESP-IDF
- Added support for turning the Secondary Plame on (previously called Front Flame, but some fireplaces have it in the back)
- Various tinkering

## Features

- ✅ Full control of ProFlame 2 fireplace systems
- ✅ Native Home Assistant integration via ESPHome
- ✅ Control power, flame height (0-6), fan speed (0-6), light level (0-6), secondary flame control
- ✅ Switch between IPI/CPI pilot modes
- ✅ Auxiliary power control
- ✅ No cloud dependency - fully local control
- ✅ Web interface for standalone control
- ✅ MQTT support (via ESPHome)

## Hardware Requirements

- **ESP32 Development Board** 
- **CC1101 RF Module** (433MHz version)
- **Jumper wires** for connections
- **3.3V power supply** (USB power from ESP32 is sufficient)

### Compatible CC1101 Modules
- Generic CC1101 modules from AliExpress/eBay
- Ensure it's the 433MHz version (not 868MHz or 915MHz)

## Wiring Diagram

### ESP32 to CC1101 Connections

```
ESP32          CC1101
-----          ------
3.3V    <-->   VCC
GND     <-->   GND
GPI021  <-->   CSN (Chip Select)
GPIO18  <-->   SCK (SPI Clock)
GPIO23  <-->   MOSI (SPI Data Out)
GPIO19  <-->   MISO (SPI Data In) [Optional for TX-only]
GPIO22  <-->   GDO0 [Optional - for future RX support]
              GDO2 [Not connected]
```

### Pinout Diagram

```
    ESP32 DEVKIT V1
    ________________
   |                |
   | EN         D23 |---- MOSI (CC1101)
   | VP         D22 |---- GDO0 (CC1101)
   | VN         TX0 |
   | D34        RX0 |
   | D35        D21 |---- CSN (CC1101)
   | D32        D19 |---- MISO (CC1101) 
   | D33        D18 |---- SCK (CC1101)
   | D25        D5  |
   | D26        TX2 |
   | D27        RX2 |
   | D14        D4  |
   | D12        D2  |
   | D13        D15 |
   | GND        GND |---- GND (CC1101)
   | VIN        3V3 |---- VCC (CC1101)
   |________________|
```

## Installation

### External Component (Git)

Use mine or fork and use your own:

```yaml
external_components:
  - source: github://marksieczkowski/esphome-proflame2@main
    components: [proflame2]
```

## Configuration

### Basic Configuration

```yaml
spi:
  clk_pin: GPIO18
  miso_pin: GPIO19
  mosi_pin: GPIO23

proflame2:
  id: fireplace
  cs_pin: GPIO21
  gdo0_pin: GPIO22 # optional
  serial_number: !secret fireplace_remote_serial_number # Not really a secret but it makes it easy to change

  power:
    name: "Power"
    icon: "mdi:fireplace"

  pilot:
    name: "Pilot Mode"
    icon: "mdi:fire"
    entity_category: config

  aux:
    name: "Aux Power"
    icon: "mdi:power-plug"

  flame:
    name: "Flame Height"
    icon: "mdi:fire"
    mode: slider

  fan:
    name: "Fan Speed"
    icon: "mdi:fan"
    mode: slider

  secondary_flame:
    name: "Secondary Flame"
    icon: "mdi:fire"
```

### Full Configuration Example

See `proflame2_fireplace.yaml` for a complete example with all options.

## Getting Your Serial Number

You have three options to get a working serial number:

### Option 1: Clone Existing Remote (Recommended)
1. Use an [RTL-SDR](https://www.rtl-sdr.com/buy-rtl-sdr-dvb-t-dongles/) or similar to capture your existing remote's signal
2. [rtl_433](https://github.com/merbanan/rtl_433) will decode the ProFlame 2 signals for you in realtime

### Option 2: Pair a new serial (Caution your old remote will stop working because you can only have one remote paired to the fireplace)
1. Use the default `0x12345678` or generate a random 24-bit number
2. Put your fireplace receiver in pairing mode (see manual)
3. Send a command with the ESP32
4. The receiver should accept and pair with this new serial

## Home Assistant Integration

Once configured and running, the fireplace will appear in Home Assistant with:

### Entities Created
- `switch.fireplace_power` - Main on/off control
- `switch.fireplace_pilot_mode` - IPI/CPI mode selection
- `switch.fireplace_aux_power` - Auxiliary outlet control
- `switch.fireplace_secondary_flame` - Secondary (aka Front) Flame on/off
- `number.fireplace_flame_height` - Flame height (0-6)
- `number.fireplace_fan_speed` - Fan speed (0-6)
- `number.fireplace_light_level` - Light brightness (0-6)

### Example Automations

#### Turn on at sunset:
```yaml
automation:
  - alias: "Fireplace Sunset"
    trigger:
      - platform: sun
        event: sunset
    action:
      - service: switch.turn_on
        entity_id: switch.fireplace_power
      - service: number.set_value
        entity_id: number.fireplace_flame_height
        data:
          value: 3
```

#### Temperature-based control:
```yaml
automation:
  - alias: "Fireplace Temperature Control"
    trigger:
      - platform: numeric_state
        entity_id: sensor.living_room_temperature
        below: 18
    action:
      - service: switch.turn_on
        entity_id: switch.fireplace_power
```

## Troubleshooting

### Fireplace doesn't respond
1. **Check wiring** - Ensure all SPI connections are correct
2. **Verify serial number** - Must match paired remote or be freshly paired
3. **Check logs** - `esphome logs my_fireplace.yaml`
4. **Verify frequency** - Some regions use 315MHz instead of 433MHz

### Intermittent control
1. **Antenna** - Ensure CC1101 antenna is connected and positioned well
2. **Distance** - Move ESP32 closer to fireplace
3. **Interference** - Check for other 433MHz devices

### Can't compile
1. **ESPHome version** - Ensure you're using ESPHome 2023.12.0 or newer
2. **Board selection** - Verify ESP32 board type matches your hardware
3. **Dependencies** - SPI component should be automatically included

## Safety Considerations

⚠️ **IMPORTANT SAFETY NOTES**:
- This project controls a gas appliance - incorrect use could be dangerous
- Always maintain the original remote as a backup
- Test thoroughly before relying on automation
- Install CO detectors near the fireplace
- Follow local codes and regulations
- Consider adding timeout automations to prevent extended operation

## Protocol Details

The ProFlame 2 uses:
- **Frequency**: 314.973 MHz (some models use 315 MHz or 433 MHz)
- **Modulation**: OOK (On-Off Keying)
- **Baud Rate**: 2400
- **Encoding**: Thomas Manchester
- **Packet**: 7 words × 13 bits = 91 bits total
- **Checksum**: Nibble-based XOR with constants

## Advanced Features

### Custom Commands

You can send custom commands by calling the component methods directly:

```cpp
id(my_fireplace)->set_flame_level(4);
id(my_fireplace)->set_fan_level(2);
```

### Receive Mode (Future)

The GDO0 pin connection enables future receive capability to:
- Detect remote control usage
- Sync state with physical remote
- Monitor fireplace status

## Contributing

Contributions are welcome! Please submit pull requests for:
- Additional fireplace model support
- Receive mode implementation
- Climate component integration
- Improved error handling

## Credits

- Original protocol reverse engineering: [johnellinwood/smartfire](https://github.com/johnellinwood/smartfire)
- ProFlame 2 protocol documentation: FCC ID T99058402300
- ESPHome CC1101 examples: [LSatan/SmartRC-CC1101-Driver-Lib](https://github.com/LSatan/SmartRC-CC1101-Driver-Lib)

## License

This project is licensed under the MIT License - see LICENSE file for details.

## Disclaimer

This project is not affiliated with, endorsed by, or connected to SIT Group, ProFlame, or any fireplace manufacturers. Use at your own risk. The authors assume no responsibility for damages or injuries resulting from the use of this software.

