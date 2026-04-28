#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/i2c/i2c.h"

namespace esphome {
namespace proflame2 {

// State-of-charge reader for the BQ27220 fuel gauge present on the LilyGo
// T-Embed CC1101 (I2C address 0x55, SDA=GPIO8, SCL=GPIO18). Polls the
// CommandStateOfCharge register (0x2C) and publishes the percentage 0..100.
//
// The chip exposes many other registers (voltage, current, temperature,
// design capacity); only state-of-charge is needed for the status bar today.
// Add per-quantity readers later if the info screen wants to show more.
class ProFlame2Battery : public sensor::Sensor,
                        public PollingComponent,
                        public i2c::I2CDevice {
 public:
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
};

}  // namespace proflame2
}  // namespace esphome
