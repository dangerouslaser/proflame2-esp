#pragma once

#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/sensor/sensor.h"
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

  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  climate::ClimateTraits traits() override;

 protected:
  void control(const climate::ClimateCall &call) override;
  void run_hysteresis_();
  void apply_fan_level_(uint8_t level);
  climate::ClimateFanMode level_to_fan_mode_(uint8_t level);

  static constexpr uint8_t HEAT_FLAME_LEVEL = 6;     // run flat-out when heating
  static constexpr uint32_t EVAL_INTERVAL_MS = 5000;  // hysteresis tick

  ProFlame2Component *parent_{nullptr};
  sensor::Sensor *sensor_{nullptr};
  float hysteresis_{0.5f};
  uint32_t last_eval_ms_{0};
  bool first_eval_{true};
};

}  // namespace proflame2
}  // namespace esphome
