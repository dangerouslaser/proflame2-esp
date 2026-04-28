#pragma once

#include "esphome/core/component.h"
#include "esphome/core/color.h"
#include "esphome/components/display/display.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/font/font.h"

#include "proflame2_cc1101.h"

namespace esphome {
namespace proflame2 {

// Standalone physical UI for the LilyGo T-Embed CC1101. Renders fireplace
// state on the ST7789V LCD and accepts encoder + button input to adjust
// power/flame/fan/light. All actions go through ProFlame2Component's existing
// public setters so HA and the local UI stay in sync without separate plumbing.
//
// Field selection is a short button-press cycle. Encoder rotation adjusts the
// selected field: ±1 for numeric levels, on/off (clockwise=ON) for power.
//
// This commit (4) ships IDLE-only behavior — no menu, no learn flow yet.
// Long-press handling is wired through to a stub for forward-compat with the
// learn-mode menu in commit 7.
class ProFlame2UI : public Component {
 public:
  void set_parent(ProFlame2Component *p) { parent_ = p; }
  void set_encoder(sensor::Sensor *s) { encoder_ = s; }
  void set_encoder_button(binary_sensor::BinarySensor *b) { encoder_button_ = b; }
  void set_pair_button(binary_sensor::BinarySensor *b) { pair_button_ = b; }
  void set_status_sensor(binary_sensor::BinarySensor *b) { status_sensor_ = b; }
  void set_battery_sensor(sensor::Sensor *s) { battery_sensor_ = s; }
  void set_font_small(font::Font *f) { font_small_ = f; }
  void set_font_medium(font::Font *f) { font_medium_ = f; }
  void set_font_large(font::Font *f) { font_large_ = f; }

  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // Called from the YAML display lambda to render the current frame. Reads
  // live state from the parent component, so always reflects HA-side changes
  // without needing explicit sync.
  void draw(display::Display &it);

 protected:
  // Two-mode encoder navigation. kNavigate: rotation moves the highlighted
  // field; click enters kEdit. kEdit: rotation adjusts the current field's
  // value; click confirms back to kNavigate. The info screen is an overlay
  // independent of this state — it's tracked by show_info_screen_.
  enum class UIState : uint8_t {
    kNavigate = 0,
    kEdit,
  };

  // Listed in the cycle order the UI will walk through on encoder rotation
  // in navigate mode. kInfo is a "menu action" entry: clicking it skips edit
  // mode and opens the info screen directly. Rotation in kEdit on kInfo is a
  // no-op.
  enum class Field : uint8_t {
    kFlame = 0,
    kFan,
    kLight,
    kSecondary,
    kPower,
    kInfo,
    kCount,
  };

  void on_encoder_delta_(int direction);  // direction is +1 or -1
  void on_button_press_();
  void on_button_release_();
  void on_pair_button_change_(bool pressed);
  void cycle_selection_();
  void cycle_selection_back_();
  void apply_delta_to_selected_(int direction);
  bool is_field_navigable_(Field f) const;
  void draw_status_bar_(display::Display &it, int width);
  void draw_idle_(display::Display &it, int width, int height);
  void draw_info_(display::Display &it, int width, int height);
  void draw_learn_(display::Display &it, int width, int height);

  const char *field_label_(Field f) const;
  int field_value_(Field f) const;

  static constexpr uint32_t kLongPressMs = 1500;

  ProFlame2Component *parent_{nullptr};
  sensor::Sensor *encoder_{nullptr};
  binary_sensor::BinarySensor *encoder_button_{nullptr};
  // Second board button (GPIO 6 on T-Embed CC1101). Long-pressing it starts
  // learn-mode pairing. Optional — null on hardware that doesn't expose a
  // dedicated pair button.
  binary_sensor::BinarySensor *pair_button_{nullptr};
  // Optional — bound to a `binary_sensor: platform: status` so the UI can
  // surface HA connectivity. Null is fine; the UI just shows "HA: ?".
  binary_sensor::BinarySensor *status_sensor_{nullptr};
  // Optional — bound to a battery-percentage sensor (e.g. proflame2_battery
  // talking to the BQ27220 fuel gauge on the T-Embed). Null = "BAT --".
  sensor::Sensor *battery_sensor_{nullptr};

  font::Font *font_small_{nullptr};
  font::Font *font_medium_{nullptr};
  font::Font *font_large_{nullptr};

  Field selected_{Field::kFlame};
  UIState ui_state_{UIState::kNavigate};
  float last_encoder_value_{0.0f};
  bool last_button_state_{false};
  uint32_t button_pressed_at_ms_{0};
  uint32_t last_interaction_ms_{0};

  // Pair-button (long-press → learn-mode) state.
  bool last_pair_button_state_{false};
  uint32_t pair_button_pressed_at_ms_{0};

  // Info screen visibility. Toggled by clicking the Info menu item; any short
  // click or encoder rotation while it's visible dismisses it.
  bool show_info_screen_{false};
};

}  // namespace proflame2
}  // namespace esphome
