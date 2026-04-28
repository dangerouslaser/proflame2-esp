#include "proflame2_climate.h"
#include "esphome/core/log.h"
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace esphome {
namespace proflame2 {

static const char *const TAG = "proflame2.climate";

void ProFlame2Climate::set_sensor(sensor::Sensor *s) {
  this->sensor_ = s;
  if (s != nullptr) {
    // Propagate NaN intentionally — when HA marks the upstream sensor as
    // unavailable/unknown (HA reboot, source-entity error, etc.) the
    // homeassistant platform publishes NaN. Letting it through keeps HA's
    // climate card honest and lets run_hysteresis_() take its NaN-safety
    // branch (which drops the burner) on the next tick.
    s->add_on_state_callback([this](float v) {
      this->current_temperature = v;
      this->publish_state();
    });
  }
}

void ProFlame2Climate::setup() {
  // Fan modes: built-in OFF/LOW/MEDIUM/HIGH only. HA's HomeKit bridge only
  // surfaces a RotationSpeed slider on the climate accessory when fan_modes
  // intersect with HA's built-in [LOW, MIDDLE, MEDIUM, HIGH]; custom strings
  // are ignored. Granularity loss vs. the fireplace's 0–6 levels is intentional —
  // users wanting fine control can still drive the existing `fan` number entity.
  // Mapping (also enforced in apply_fan_level_): OFF→0, LOW→2, MEDIUM→4, HIGH→6.

  // Restore mode + target_temperature from RTC if a previous boot had them set.
  auto restore = this->restore_state_();
  if (restore.has_value()) {
    restore->to_call(this).perform();
  } else {
    this->mode = climate::CLIMATE_MODE_OFF;
    this->target_temperature = 21.0f;  // ~70°F sensible default
  }

  if (this->sensor_ != nullptr && this->sensor_->has_state()) {
    this->current_temperature = this->sensor_->state;
  }

  // Default fan mode reflects the parent's current cached fan level.
  if (this->parent_ != nullptr) {
    this->set_fan_mode_(this->level_to_fan_mode_(this->parent_->current_state_.fan_level));
  }

  this->publish_state();
}

climate::ClimateTraits ProFlame2Climate::traits() {
  auto t = climate::ClimateTraits();
  t.set_supports_current_temperature(true);
  t.set_supports_action(true);
  t.set_supports_two_point_target_temperature(false);
  t.add_supported_mode(climate::CLIMATE_MODE_OFF);
  t.add_supported_mode(climate::CLIMATE_MODE_HEAT);
  // Built-in fan modes only — required for HA HomeKit's RotationSpeed slider
  // to appear on the climate accessory.
  t.add_supported_fan_mode(climate::CLIMATE_FAN_OFF);
  t.add_supported_fan_mode(climate::CLIMATE_FAN_LOW);
  t.add_supported_fan_mode(climate::CLIMATE_FAN_MEDIUM);
  t.add_supported_fan_mode(climate::CLIMATE_FAN_HIGH);
  t.set_visual_min_temperature(15.5f);   // ~60°F
  t.set_visual_max_temperature(29.5f);   // ~85°F
  t.set_visual_target_temperature_step(0.5f);
  t.set_visual_current_temperature_step(0.1f);
  return t;
}

void ProFlame2Climate::control(const climate::ClimateCall &call) {
  if (call.get_mode().has_value()) {
    auto new_mode = *call.get_mode();
    if (new_mode != this->mode) {
      this->mode = new_mode;
      if (new_mode == climate::CLIMATE_MODE_OFF) {
        // Clean stop: drop secondary flame + fan and force power off.
        if (this->parent_ != nullptr && this->parent_->current_state_.power) {
          this->parent_->set_secondary_flame(false);
          this->parent_->set_fan_level(0);
          this->parent_->set_power(false);
          this->parent_->queue_send();
        }
        this->action = climate::CLIMATE_ACTION_OFF;
      } else if (new_mode == climate::CLIMATE_MODE_HEAT) {
        this->first_eval_ = true;  // force immediate eval
      }
    }
  }

  if (call.get_target_temperature().has_value()) {
    this->target_temperature = *call.get_target_temperature();
  }

  // Built-in fan modes: OFF→0, LOW→2, MEDIUM→4, HIGH→6.
  if (call.get_fan_mode().has_value()) {
    auto fm = *call.get_fan_mode();
    uint8_t lvl;
    switch (fm) {
      case climate::CLIMATE_FAN_OFF:    lvl = 0; break;
      case climate::CLIMATE_FAN_LOW:    lvl = 2; break;
      case climate::CLIMATE_FAN_MEDIUM: lvl = 4; break;
      case climate::CLIMATE_FAN_HIGH:   lvl = 6; break;
      default:
        ESP_LOGW(TAG, "Unsupported fan_mode %d; ignoring", static_cast<int>(fm));
        lvl = 0xFF;
    }
    if (lvl != 0xFF) {
      this->apply_fan_level_(lvl);
    }
  }

  this->publish_state();

  if (this->mode == climate::CLIMATE_MODE_HEAT) {
    this->run_hysteresis_();
  }
}

void ProFlame2Climate::apply_fan_level_(uint8_t level) {
  if (this->parent_ == nullptr || level > 6) {
    return;
  }
  this->set_fan_mode_(this->level_to_fan_mode_(level));
  if (this->parent_->current_state_.fan_level != level) {
    this->parent_->set_fan_level(level);
    this->parent_->queue_send();
  }
}

// Map raw fan level (0–6, including off-mapping levels via the climate dropdown)
// back to the closest built-in fan mode for HA/HomeKit display.
climate::ClimateFanMode ProFlame2Climate::level_to_fan_mode_(uint8_t level) {
  if (level == 0) return climate::CLIMATE_FAN_OFF;
  if (level <= 2) return climate::CLIMATE_FAN_LOW;
  if (level <= 4) return climate::CLIMATE_FAN_MEDIUM;
  return climate::CLIMATE_FAN_HIGH;
}

void ProFlame2Climate::loop() {
  const uint32_t now = millis();
  if (!this->first_eval_ && (now - this->last_eval_ms_) < EVAL_INTERVAL_MS) {
    return;
  }
  this->last_eval_ms_ = now;
  this->first_eval_ = false;
  this->run_hysteresis_();
}

void ProFlame2Climate::run_hysteresis_() {
  if (this->parent_ == nullptr) {
    return;
  }

  if (this->mode == climate::CLIMATE_MODE_OFF) {
    if (this->action != climate::CLIMATE_ACTION_OFF) {
      this->action = climate::CLIMATE_ACTION_OFF;
      this->publish_state();
    }
    return;
  }

  // HEAT mode below.
  const float current = this->current_temperature;
  const float target = this->target_temperature;
  if (std::isnan(current) || std::isnan(target)) {
    // Lost the temperature feed (HA unavailable, helper error, etc.). This is
    // a gas appliance — fail safe by dropping the burner if it's currently on.
    bool republish = false;
    if (this->parent_ != nullptr && this->parent_->current_state_.power) {
      ESP_LOGW(TAG, "Temperature unavailable; shutting fireplace down");
      this->parent_->set_secondary_flame(false);
      this->parent_->set_fan_level(0);
      this->parent_->set_power(false);
      this->parent_->queue_send();
      republish = true;
    }
    if (this->action != climate::CLIMATE_ACTION_IDLE) {
      this->action = climate::CLIMATE_ACTION_IDLE;
      republish = true;
    }
    if (republish) {
      this->publish_state();
    }
    return;
  }

  const bool power_on = this->parent_->current_state_.power;
  bool changed = false;

  if (!power_on && current < (target - this->hysteresis_)) {
    // Apply user-configured heat behavior in a single packet alongside power-on.
    // Order matters: set_power(true) auto-defaults secondary flame to ON, so
    // call it BEFORE set_secondary_flame() with the user's heat-config value
    // so the user's choice (which may be OFF) wins over the auto-default.
    this->parent_->set_flame_level(this->get_heat_flame_level_());
    this->parent_->set_fan_level(this->get_heat_fan_level_());
    this->parent_->set_power(true);
    this->parent_->set_secondary_flame(this->get_heat_secondary_flame_());
    this->parent_->queue_send();
    if (this->action != climate::CLIMATE_ACTION_HEATING) {
      this->action = climate::CLIMATE_ACTION_HEATING;
      changed = true;
    }
  } else if (power_on && current >= (target + this->hysteresis_)) {
    // Cycle off: drop fan + secondary along with power so the fireplace doesn't
    // sit blowing room-temp air after the burner shuts down.
    this->parent_->set_secondary_flame(false);
    this->parent_->set_fan_level(0);
    this->parent_->set_power(false);
    this->parent_->queue_send();
    if (this->action != climate::CLIMATE_ACTION_IDLE) {
      this->action = climate::CLIMATE_ACTION_IDLE;
      changed = true;
    }
  } else {
    // Hold band — keep action consistent with actual power state.
    auto desired_action = power_on ? climate::CLIMATE_ACTION_HEATING : climate::CLIMATE_ACTION_IDLE;
    if (this->action != desired_action) {
      this->action = desired_action;
      changed = true;
    }
  }

  if (changed) {
    this->publish_state();
  }
}

uint8_t ProFlame2Climate::get_heat_flame_level_() const {
  if (this->heat_flame_level_ != nullptr && this->heat_flame_level_->has_state()) {
    int v = static_cast<int>(this->heat_flame_level_->state);
    if (v < 1) v = 1;
    if (v > 6) v = 6;
    return static_cast<uint8_t>(v);
  }
  return 6;  // unconfigured → run flat-out
}

uint8_t ProFlame2Climate::get_heat_fan_level_() const {
  if (this->heat_fan_level_ != nullptr && this->heat_fan_level_->has_state()) {
    int v = static_cast<int>(this->heat_fan_level_->state);
    if (v < 0) v = 0;
    if (v > 6) v = 6;
    return static_cast<uint8_t>(v);
  }
  return 0;  // unconfigured → no fan
}

bool ProFlame2Climate::get_heat_secondary_flame_() const {
  if (this->heat_secondary_flame_ != nullptr) {
    return this->heat_secondary_flame_->state;
  }
  return false;  // unconfigured → off
}

}  // namespace proflame2
}  // namespace esphome
