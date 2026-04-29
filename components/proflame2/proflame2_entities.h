#pragma once

// Entity subclasses (switches, numbers, light, buttons) that bridge ESPHome
// platform entities to ProFlame2Component. Extracted from proflame2_cc1101.h
// so files that don't define entity-method bodies don't have to pull these
// in.

#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/number/number.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/button/button.h"

namespace esphome {
namespace proflame2 {

// Forward declaration — the entity classes only need to hold a pointer and
// call public methods; the full type comes from proflame2_cc1101.h in the
// .cpp files that actually implement those calls.
class ProFlame2Component;

// Switch implementations
class ProFlame2PowerSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(ProFlame2Component *parent) { this->parent_ = parent; }
  void write_state(bool state) override;

 protected:
  ProFlame2Component *parent_;
};

class ProFlame2PilotSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(ProFlame2Component *parent) { this->parent_ = parent; }
  void write_state(bool state) override;

 protected:
  ProFlame2Component *parent_;
};

class ProFlame2AuxSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(ProFlame2Component *parent) { this->parent_ = parent; }
  void write_state(bool state) override;

 protected:
  ProFlame2Component *parent_;
};

// Number component implementations
class ProFlame2FlameNumber : public number::Number, public Component {
 public:
  void set_parent(ProFlame2Component *parent) { this->parent_ = parent; }
  void control(float value) override;

 protected:
  ProFlame2Component *parent_;
};

class ProFlame2FanNumber : public number::Number, public Component {
 public:
  void set_parent(ProFlame2Component *parent) { this->parent_ = parent; }
  void control(float value) override;

 protected:
  ProFlame2Component *parent_;
};

// Fireplace light: a brightness-only light. Hardware constraint — the light
// physically only operates while the burner is running, so we reject control
// attempts whenever power is off.
class ProFlame2Light : public light::LightOutput {
 public:
  void set_parent(ProFlame2Component *parent) { this->parent_ = parent; }
  light::LightTraits get_traits() override {
    auto traits = light::LightTraits();
    traits.set_supported_color_modes({light::ColorMode::BRIGHTNESS});
    return traits;
  }
  void write_state(light::LightState *state) override;

 protected:
  ProFlame2Component *parent_{nullptr};
};

class ProFlame2SecondaryFlameSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(ProFlame2Component *parent) { this->parent_ = parent; }
  void write_state(bool state) override;

 protected:
  ProFlame2Component *parent_;
};

// Persistent number entity used by the climate as "what flame/fan level should
// the burner run at when the climate auto-activates?". Pure config — no parent
// callback. The value survives reboots via global_preferences.
class ProFlame2ConfigNumber : public number::Number, public Component {
 public:
  void set_default_value(float v) { this->default_value_ = v; }
  void setup() override;
  void control(float value) override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  float default_value_{0.0f};
  ESPPreferenceObject pref_;
};

// Persistent switch for "should secondary flame be on while heating?". Pure
// config — restore_mode is honored via switch base class.
class ProFlame2HeatSecondaryFlameSwitch : public switch_::Switch, public Component {
 public:
  void setup() override;
  void write_state(bool state) override { this->publish_state(state); }
  float get_setup_priority() const override { return setup_priority::DATA; }
};

// HA-side trigger to start the learn-mode pairing flow without needing to
// physically press the on-device pair button. T-Embed users still get the
// on-device encoder long-press shortcut.
class ProFlame2PairButton : public button::Button, public Component {
 public:
  void set_parent(ProFlame2Component *parent) { this->parent_ = parent; }

 protected:
  void press_action() override;

  ProFlame2Component *parent_{nullptr};
};

// HA-side commit for a converged learn-mode candidate. Necessary on plain
// ESP32 boards where there's no on-device encoder long-press to confirm
// the captured serial + ECC. learn_confirm() is a no-op outside kConverged
// so calling this at the wrong time is harmless.
class ProFlame2PairConfirmButton : public button::Button, public Component {
 public:
  void set_parent(ProFlame2Component *parent) { this->parent_ = parent; }

 protected:
  void press_action() override;

  ProFlame2Component *parent_{nullptr};
};

// HA-side abort for an in-flight learn-mode flow. Idempotent — calling it
// from kIdle is a no-op, so it's safe to leave in HA as a "stop pairing"
// escape hatch.
class ProFlame2PairCancelButton : public button::Button, public Component {
 public:
  void set_parent(ProFlame2Component *parent) { this->parent_ = parent; }

 protected:
  void press_action() override;

  ProFlame2Component *parent_{nullptr};
};

}  // namespace proflame2
}  // namespace esphome
