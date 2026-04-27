#pragma once

#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/number/number.h"
#include "esphome/components/switch/switch.h"
#include "proflame2_cc1101.h"

namespace esphome {
namespace proflame2 {

// Bang-bang HEAT/OFF thermostat layered on top of ProFlame2Component.
// current_temperature comes from an external HA sensor; the loop drives the
// fireplace power + flame at full output until the room reaches target.
class ProFlame2Climate : public climate::Climate, public Component {
 public:
  void set_parent(ProFlame2Component *p) { parent_ = p; }
  void set_sensor(sensor::Sensor *s);
  void set_hysteresis(float h) { hysteresis_ = h; }
  void set_heat_flame_level(number::Number *n) { heat_flame_level_ = n; }
  void set_heat_fan_level(number::Number *n) { heat_fan_level_ = n; }
  void set_heat_secondary_flame(switch_::Switch *s) { heat_secondary_flame_ = s; }

  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  climate::ClimateTraits traits() override;

 protected:
  void control(const climate::ClimateCall &call) override;
  void run_hysteresis_();
  void apply_fan_level_(uint8_t level);
  climate::ClimateFanMode level_to_fan_mode_(uint8_t level);

  // Read configured behaviors. Falls back to safe defaults if the optional
  // config entity wasn't wired up (or hasn't restored a value yet).
  uint8_t get_heat_flame_level_() const;
  uint8_t get_heat_fan_level_() const;
  bool get_heat_secondary_flame_() const;

  static constexpr uint32_t EVAL_INTERVAL_MS = 5000;  // hysteresis tick

  ProFlame2Component *parent_{nullptr};
  sensor::Sensor *sensor_{nullptr};
  number::Number *heat_flame_level_{nullptr};
  number::Number *heat_fan_level_{nullptr};
  switch_::Switch *heat_secondary_flame_{nullptr};
  float hysteresis_{0.5f};
  uint32_t last_eval_ms_{0};
  bool first_eval_{true};
};

}  // namespace proflame2
}  // namespace esphome
