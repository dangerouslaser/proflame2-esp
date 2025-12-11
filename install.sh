#!/bin/bash
# ProFlame 2 ESPHome Component Installation Script

echo "================================================"
echo "ProFlame 2 ESPHome Component Installer"
echo "================================================"

# Check if we're in ESPHome directory
if [ ! -d "custom_components" ]; then
    echo "Creating custom_components directory..."
    mkdir -p custom_components
fi

# Create proflame2 component directory
echo "Installing ProFlame 2 component..."
cp -r ./custom_components/proflame2 ./custom_components/

# Check if examples should be installed
read -p "Do you want to install example configurations? (y/n): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    cp ./examples/proflame2_fireplace.yaml ./
    echo "Example configuration copied to proflame2_fireplace.yaml"
    echo "Please edit this file with your WiFi credentials and pin assignments"
fi

# Check for Home Assistant integration
read -p "Do you want to install Lovelace card examples? (y/n): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo "Lovelace card examples are in examples/lovelace_cards.yaml"
    echo "Copy the relevant sections to your Home Assistant configuration"
fi

echo ""
echo "================================================"
echo "Installation Complete!"
echo "================================================"
echo ""
echo "Next steps:"
echo "1. Edit proflame2_fireplace.yaml with your settings:"
echo "   - WiFi credentials"
echo "   - Pin assignments (if different)"
echo "   - Serial number (see README for details)"
echo ""
echo "2. Compile and upload to ESP32:"
echo "   esphome run proflame2_fireplace.yaml"
echo ""
echo "3. The device will appear in Home Assistant automatically"
echo ""
echo "For detailed instructions, see README.md"
echo ""
echo "Hardware connections:"
echo "  ESP32  -> CC1101"
echo "  3.3V   -> VCC"
echo "  GND    -> GND"
echo "  GPIO5  -> CSN"
echo "  GPIO18 -> SCK"
echo "  GPIO23 -> MOSI"
echo "  GPIO19 -> MISO (optional)"
echo "  GPIO4  -> GDO0 (optional)"
echo ""
echo "Happy automating! 🔥"
