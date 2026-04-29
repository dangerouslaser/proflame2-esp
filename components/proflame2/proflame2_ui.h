#pragma once

#include "esphome/core/defines.h"

#ifdef USE_DISPLAY

#include "esphome/core/component.h"
#include "esphome/core/color.h"
#include "esphome/components/display/display.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/font/font.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/select/select.h"
#include "esphome/components/time/real_time_clock.h"

#include "proflame2_cc1101.h"
#include "proflame2_climate.h"

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
  // Optional. When set, the UI auto-dims the backlight after kBacklightIdleMs
  // of no encoder/button activity, and wakes it on any input. Null = always on.
  void set_backlight(light::LightState *l) { backlight_ = l; }
  // Optional. When set, the UI exposes Field::kLeds (a settings cog) that
  // toggles this switch's state. Wire it to the same template switch that
  // gates the WS2812 strip so HA + device share one source of truth.
  void set_leds_switch(switch_::Switch *s) { leds_switch_ = s; }
  // Optional. When set, the status-bar battery indicator renders as a
  // graphical bar; when off, falls back to "BAT NN%" text. Same single-
  // source-of-truth pattern as leds_switch_ — accessible from HA and from
  // the on-device settings page.
  void set_battery_bar_switch(switch_::Switch *s) { battery_bar_switch_ = s; }
  // Optional. When ON, the LCD shows a large HH:MM clock when the user is
  // idle past the backlight timeout, instead of turning the backlight off.
  // Requires set_time() to be wired so we have a clock source.
  void set_clock_on_idle_switch(switch_::Switch *s) { clock_on_idle_switch_ = s; }
  // Optional. When ON, encoder rotation direction is flipped — useful for
  // users whose physical encoder feels backwards. Read at delta time so
  // nothing else has to know.
  void set_encoder_invert_switch(switch_::Switch *s) { encoder_invert_switch_ = s; }
  // Optional. Template select with options "15s" / "30s" / "1m" / "5m" /
  // "never" controlling how long until the backlight goes off (or the clock
  // screensaver activates) on idle. Falls back to 30 s when unset.
  void set_backlight_timeout_select(select::Select *s) { backlight_timeout_select_ = s; }
  // Optional. Time source for the clock screensaver. Anything inheriting
  // from RealTimeClock works (homeassistant_time, sntp, etc).
  void set_time(time::RealTimeClock *t) { time_ = t; }
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
  // in navigate mode. kSettings is a "menu action" entry: clicking it skips
  // edit mode and opens the settings overlay. Rotation in kEdit on it is a
  // no-op. (Info now lives inside the settings page.)
  enum class Field : uint8_t {
    kFlame = 0,
    kFan,
    kLight,
    kSecondary,
    kPower,
    kClimate,   // Click → opens the climate editor (mode / target temp / fan).
    kSettings,
    kCount,
  };

  // Top-level UI screen the user is currently looking at. The learn flow has
  // its own override (drawn whenever parent_->get_learn_state() != kIdle);
  // this enum covers everything else. Replaces the old show_info_screen_ bool.
  enum class View : uint8_t {
    kIdle = 0,    // The main field list (current default).
    kInfo,        // Full-screen system info page (was: show_info_screen_).
    kSettings,    // Full-screen scrollable settings list.
    kClimate,     // Full-screen climate editor (mode / target temp / fan).
  };

  // Climate editor sub-fields — same nav pattern as the main page (cycle in
  // kNavigate, click-and-edit on the temp / heat-config rows). Mode / Fan
  // toggle in place. The four kHeat* rows mirror the HA-side config entities
  // (heat_flame_level / heat_fan_level / heat_light_level / heat_secondary_flame)
  // so users can tune defaults without HA — and they pick up automatically when
  // the climate next engages the burner.
  enum class ClimateField : uint8_t {
    kMode = 0,
    kTargetTemp,
    kFanMode,
    kHeatFlame,
    kHeatFan,
    kHeatLight,
    kHeatSecondary,
    kBack,
    kCount,
  };

  // Settings-page items. Cycle order matches what users see top-to-bottom on
  // the LCD. Toggleable items live alongside one-shot actions; on_settings_click
  // dispatches by item type.
  enum class SettingItem : uint8_t {
    kLeds = 0,
    kBatteryBar,
    kClockOnIdle,
    kBacklightTimeout,
    kEncoderInvert,
    kInfo,           // Opens the read-only diagnostic info screen.
    kClearPairing,
    kReboot,
    kBack,
    kCount,
  };

  void on_encoder_delta_(int direction);  // direction is +1 or -1
  void on_button_press_();
  void on_button_release_();
  void on_pair_button_change_(bool pressed);

  // Settings-page input handlers. Mirror the main-page versions but operate
  // on selected_setting_ + the SettingItem enum. Click activates the current
  // item; rotation moves through the list. Long-press from settings exits
  // back to idle (so users can always escape if they got there by accident).
  void on_settings_click_();
  void on_settings_rotate_(int direction);

  // Climate-page input handlers — same shape as settings, but the temp
  // field uses kEdit mode so a follow-up rotation adjusts the value
  // instead of moving to the next field.
  void on_climate_click_();
  void on_climate_rotate_(int direction);

  // Top-level click dispatch — picks the right handler based on view_ + the
  // learn-mode state. Pulled out of on_button_release_ to keep that focused
  // on press/release timing.
  void handle_click_(bool long_press);
  // Synchronously turn the backlight back on if it's currently off. Called
  // from input handlers so the screen responds instantly rather than waiting
  // for the next loop() reconciliation.
  void wake_backlight_();
  // Returns 0 ms for binary backlights (no brightness curve) and ~150 ms
  // for PWM-dimmable ones, so the same idle-dim path works either way
  // without spamming "transitions not supported" warnings.
  uint32_t backlight_transition_ms_() const;
  void cycle_selection_();
  void cycle_selection_back_();
  void apply_delta_to_selected_(int direction);
  bool is_field_navigable_(Field f) const;
  void draw_status_bar_(display::Display &it, int width);
  void draw_idle_(display::Display &it, int width, int height);
  void draw_info_(display::Display &it, int width, int height);
  void draw_learn_(display::Display &it, int width, int height);
  void draw_settings_(display::Display &it, int width, int height);
  void draw_clock_(display::Display &it, int width, int height);
  void draw_climate_(display::Display &it, int width, int height);

  // Climate-page label helpers. Static so they don't pull in any state.
  static const char *climate_field_label_(ClimateField c);
  static const char *climate_mode_label_(climate::ClimateMode m);
  static const char *climate_fan_label_(climate::ClimateFanMode f);

  // Effective backlight idle timeout in ms. Reads backlight_timeout_select_
  // when wired ("15s" / "30s" / "1m" / "5m" / "never" → ms / UINT32_MAX),
  // falls back to kBacklightIdleMs when unset.
  uint32_t backlight_idle_ms_() const;
  // True iff the user has been idle past the backlight timeout AND the
  // clock-on-idle switch is on AND we have a time source. The clock view
  // wins over kIdle / kInfo / kSettings (but not learn).
  bool is_clock_screensaver_active_() const;

  const char *field_label_(Field f) const;
  int field_value_(Field f) const;
  const char *setting_label_(SettingItem s) const;
  // Returns the per-item value-column text. Toggleable items render
  // "ON"/"OFF"; one-shot actions render an arrow ">>" so the selection
  // affordance reads as "click me" rather than "edit me".
  const char *setting_value_(SettingItem s) const;

  static constexpr uint32_t kLongPressMs = 1500;
  // Idle time before the backlight goes off. Picked to feel like a phone:
  // long enough that you can read the screen, short enough that the LCD
  // isn't burning power on a battery-powered device when nobody's looking.
  static constexpr uint32_t kBacklightIdleMs = 30000;

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
  // Optional — bound to a binary `light` driving the LCD backlight. Null
  // means we don't manage backlight (e.g. plain ESP32 builds with no LCD).
  light::LightState *backlight_{nullptr};
  // Optional — switch entity exposed to HA *and* read/written from the
  // device UI's settings cog (Field::kLeds) to enable/disable the WS2812
  // status LEDs. Single source of truth for both surfaces.
  switch_::Switch *leds_switch_{nullptr};
  // Optional — same pattern as leds_switch_, gates the status-bar battery
  // bar (vs. "BAT NN%" text fallback when off / unset).
  switch_::Switch *battery_bar_switch_{nullptr};
  // Optional — when ON and the user is idle past the backlight timeout,
  // the LCD draws a large clock instead of going dark. Requires time_.
  switch_::Switch *clock_on_idle_switch_{nullptr};
  // Optional — flips encoder rotation direction in software when ON.
  switch_::Switch *encoder_invert_switch_{nullptr};
  // Optional — drives kBacklightIdleMs at runtime ("15s" / "30s" / "1m" /
  // "5m" / "never"). Falls back to 30 s if unset.
  select::Select *backlight_timeout_select_{nullptr};
  // Optional — clock source for the screensaver. Null = clock-on-idle is
  // a no-op (we never have a time to draw).
  time::RealTimeClock *time_{nullptr};

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

  // Top-level screen the user is on. Replaces show_info_screen_ — kInfo and
  // kSettings are now full overlays, kIdle is the default field list.
  View view_{View::kIdle};
  // Cursor inside the settings list. Reset to kLeds whenever the user enters
  // the page so they always start from the top.
  SettingItem selected_setting_{SettingItem::kLeds};
  // Cursor inside the climate editor. Same reset-on-entry pattern.
  ClimateField selected_climate_{ClimateField::kMode};
};

}  // namespace proflame2
}  // namespace esphome

#endif  // USE_DISPLAY
