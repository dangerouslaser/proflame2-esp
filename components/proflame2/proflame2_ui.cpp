#include "proflame2_ui.h"

#ifdef USE_DISPLAY

#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/application.h"
#include "esphome/core/version.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/components/network/ip_address.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace esphome {
namespace proflame2 {

static const char *const TAG = "proflame2.ui";

// Fireplace-themed palette. Used sparingly — most text stays white for
// legibility on the small 320×170 panel. kAccent picks up the warm orange of
// a flame and is reserved for the selection indicator. kGreen / kAmber / kRed
// are reserved for state cues (HA online, battery tier, ON/OFF).
namespace {
const Color kWhite{0xFF, 0xFF, 0xFF};
const Color kDim{0x80, 0x80, 0x80};
const Color kAccent{0xFF, 0x80, 0x30};
const Color kGreen{0x40, 0xC0, 0x40};
const Color kAmber{0xFF, 0xB0, 0x00};
const Color kRed{0xFF, 0x60, 0x60};

// Flame value color by level — red→orange→amber as the burner ramps. Level 0
// renders dim (off).
Color flame_color_for_level(int level) {
  if (level <= 0) return kDim;
  if (level <= 2) return Color(0xFF, 0x50, 0x40);
  if (level <= 4) return Color(0xFF, 0x80, 0x30);
  return Color(0xFF, 0xB0, 0x20);
}

// Battery tier color: green ≥ 40%, amber 20–39%, red < 20%.
Color battery_color_for_pct(int pct) {
  if (pct >= 40) return kGreen;
  if (pct >= 20) return kAmber;
  return kRed;
}
}  // namespace

void ProFlame2UI::setup() {
  if (this->parent_ == nullptr) {
    ESP_LOGE(TAG, "No parent ProFlame2Component — UI inert");
    this->mark_failed();
    return;
  }

  if (this->encoder_ != nullptr) {
    this->last_encoder_value_ = this->encoder_->state;
    this->encoder_->add_on_state_callback([this](float v) {
      const float old = this->last_encoder_value_;
      if (v == old) {
        return;
      }
      int direction = (v > old) ? +1 : -1;
      this->last_encoder_value_ = v;
      // Optional software invert — flip the sign at the entry point so
      // every downstream handler still sees a normalised "+1 / -1".
      if (this->encoder_invert_switch_ != nullptr &&
          this->encoder_invert_switch_->state) {
        direction = -direction;
      }
      this->on_encoder_delta_(direction);
    });
  } else {
    ESP_LOGW(TAG, "No encoder bound — rotation input disabled");
  }

  if (this->encoder_button_ != nullptr) {
    this->encoder_button_->add_on_state_callback([this](bool pressed) {
      if (pressed && !this->last_button_state_) {
        this->on_button_press_();
      } else if (!pressed && this->last_button_state_) {
        this->on_button_release_();
      }
      this->last_button_state_ = pressed;
    });
  } else {
    ESP_LOGW(TAG, "No encoder button bound — selection input disabled");
  }

  if (this->pair_button_ != nullptr) {
    this->pair_button_->add_on_state_callback([this](bool pressed) {
      this->on_pair_button_change_(pressed);
    });
  }

  this->last_interaction_ms_ = millis();
}

void ProFlame2UI::loop() {
  // Backlight auto-off after backlight_idle_ms_() of no input. Anything that
  // bumps last_interaction_ms_ in the input handlers also calls
  // wake_backlight_(), so the screen returns immediately on a key press
  // even if the actual reconciliation here is on the next loop tick.
  // When clock-on-idle is enabled, the backlight stays on past the timeout
  // and draw() switches to the clock view instead — same ms threshold.
  if (this->backlight_ != nullptr) {
    const uint32_t idle_ms = millis() - this->last_interaction_ms_;
    const bool past_timeout = idle_ms >= this->backlight_idle_ms_();
    const bool clock_active = this->is_clock_screensaver_active_();
    const bool should_be_on = !past_timeout || clock_active;
    if (this->backlight_->remote_values.is_on() != should_be_on) {
      auto call = should_be_on ? this->backlight_->turn_on()
                               : this->backlight_->turn_off();
      call.set_transition_length(this->backlight_transition_ms_());
      call.perform();
    }
  }
}

uint32_t ProFlame2UI::backlight_idle_ms_() const {
  if (this->backlight_timeout_select_ == nullptr) {
    return kBacklightIdleMs;
  }
  const auto opt = this->backlight_timeout_select_->current_option();
  if (opt.empty()) {
    // Select hasn't restored yet — fall back to default until it does.
    return kBacklightIdleMs;
  }
  const char *s = opt.c_str();
  if (std::strcmp(s, "never") == 0) return UINT32_MAX;
  if (std::strcmp(s, "15s")   == 0) return 15000;
  if (std::strcmp(s, "30s")   == 0) return 30000;
  if (std::strcmp(s, "1m")    == 0) return 60000;
  if (std::strcmp(s, "5m")    == 0) return 300000;
  return kBacklightIdleMs;
}

bool ProFlame2UI::is_clock_screensaver_active_() const {
  if (this->clock_on_idle_switch_ == nullptr ||
      !this->clock_on_idle_switch_->state) {
    return false;
  }
  if (this->time_ == nullptr) {
    return false;
  }
  // Don't kick in until we've actually exceeded the (configured) backlight
  // timeout — otherwise the clock would replace the active UI instantly.
  const uint32_t idle_ms = millis() - this->last_interaction_ms_;
  return idle_ms >= this->backlight_idle_ms_();
}

void ProFlame2UI::wake_backlight_() {
  if (this->backlight_ == nullptr) {
    return;
  }
  if (!this->backlight_->remote_values.is_on()) {
    auto call = this->backlight_->turn_on();
    call.set_transition_length(this->backlight_transition_ms_() / 2);
    call.perform();
  }
}

uint32_t ProFlame2UI::backlight_transition_ms_() const {
  // Binary lights snap on/off; passing a non-zero transition length emits
  // a per-call "transitions not supported" warning on every idle-dim cycle.
  // PWM-dimmable backlights (if added later) get the smooth fade.
  if (this->backlight_ == nullptr) {
    return 0;
  }
  const auto &modes = this->backlight_->get_traits().get_supported_color_modes();
  const bool binary_only =
      modes.size() == 1 && *modes.begin() == light::ColorMode::ON_OFF;
  return binary_only ? 0 : 150;
}

void ProFlame2UI::on_encoder_delta_(int direction) {
  this->last_interaction_ms_ = millis();
  // First rotation of a wake-up acts like a normal turn (we don't swallow
  // it). Users can read the screen while it brightens — feels responsive.
  this->wake_backlight_();
  // Encoder rotation has no role in learn-mode (start/confirm/cancel are all
  // button-driven). Drop deltas while a learn flow is active so accidental
  // bumps don't mutate the underlying control state mid-pairing.
  if (this->parent_ != nullptr &&
      this->parent_->get_learn_state() !=
          ProFlame2Component::LearnState::kIdle) {
    return;
  }
  // Settings page: rotation moves the cursor through the settings list.
  if (this->view_ == View::kSettings) {
    this->on_settings_rotate_(direction);
    return;
  }
  // Climate page: rotation moves the cursor or adjusts the temp depending
  // on the kEdit / kNavigate state.
  if (this->view_ == View::kClimate) {
    this->on_climate_rotate_(direction);
    return;
  }
  // Any rotation dismisses the info screen and returns to idle.
  if (this->view_ == View::kInfo) {
    this->view_ = View::kIdle;
    return;
  }
  // Navigate mode: rotation moves the selection. Edit mode: rotation adjusts
  // the current field's value.
  if (this->ui_state_ == UIState::kNavigate) {
    if (direction > 0) {
      this->cycle_selection_();
    } else {
      this->cycle_selection_back_();
    }
    return;
  }
  this->apply_delta_to_selected_(direction);
}

void ProFlame2UI::on_pair_button_change_(bool pressed) {
  // Long-press of the dedicated user button (GPIO 6 on T-Embed CC1101) starts
  // learn-mode pairing. Mirrors the encoder long-press behavior so users
  // discover whichever they happen to find first.
  this->last_interaction_ms_ = millis();
  this->wake_backlight_();
  if (pressed && !this->last_pair_button_state_) {
    this->pair_button_pressed_at_ms_ = millis();
  } else if (!pressed && this->last_pair_button_state_) {
    const uint32_t held = millis() - this->pair_button_pressed_at_ms_;
    if (held >= kLongPressMs && this->parent_ != nullptr &&
        this->parent_->get_learn_state() ==
            ProFlame2Component::LearnState::kIdle) {
      ESP_LOGI(TAG, "Pair button long-press → entering learn-mode");
      this->ui_state_ = UIState::kNavigate;
      this->parent_->learn_start();
    }
  }
  this->last_pair_button_state_ = pressed;
}

void ProFlame2UI::on_button_press_() {
  this->last_interaction_ms_ = millis();
  this->button_pressed_at_ms_ = this->last_interaction_ms_;
  this->wake_backlight_();
  // No action on press — wait for release so we can distinguish short/long.
}

void ProFlame2UI::on_button_release_() {
  const uint32_t now = millis();
  const uint32_t held_for = now - this->button_pressed_at_ms_;
  this->last_interaction_ms_ = now;

  if (this->parent_ == nullptr) {
    return;
  }

  const auto learn_state = this->parent_->get_learn_state();
  const bool long_press = held_for >= kLongPressMs;

  // Learn flow takes priority — its UI completely replaces the idle/info one.
  if (learn_state != ProFlame2Component::LearnState::kIdle) {
    switch (learn_state) {
      case ProFlame2Component::LearnState::kListening:
      case ProFlame2Component::LearnState::kCapturing:
      case ProFlame2Component::LearnState::kFailed:
        ESP_LOGI(TAG, "Cancelling learn-mode");
        this->parent_->learn_cancel();
        break;
      case ProFlame2Component::LearnState::kConverged:
        if (long_press) {
          ESP_LOGI(TAG, "Confirming learn-mode (long press)");
          this->parent_->learn_confirm();
        } else {
          ESP_LOGI(TAG, "Cancelling learn-mode (short press at confirm)");
          this->parent_->learn_cancel();
        }
        break;
      case ProFlame2Component::LearnState::kPersisted:
        break;
      default:
        break;
    }
    return;
  }

  // Settings page: short click activates the highlighted item; long-press
  // is a no-op (use the BACK item to exit). Silencing long-press here also
  // stops it falling through to learn-mode start.
  if (this->view_ == View::kSettings) {
    if (!long_press) {
      this->on_settings_click_();
    }
    return;
  }

  // Climate page: same pattern — short click activates the field, long-
  // press is a no-op. Click on TARGET TEMP toggles edit mode (next rotation
  // adjusts the value instead of moving the cursor).
  if (this->view_ == View::kClimate) {
    if (!long_press) {
      this->on_climate_click_();
    }
    return;
  }

  // Info screen: any short click dismisses it (treats the second click as
  // "confirm/exit"). Long-press is silenced here so users can't accidentally
  // drop into learn-mode from inside info.
  if (this->view_ == View::kInfo) {
    if (!long_press) {
      this->view_ = View::kIdle;
    }
    return;
  }

  // Idle screen.
  if (long_press) {
    ESP_LOGI(TAG, "Encoder long press → entering learn-mode");
    this->ui_state_ = UIState::kNavigate;
    this->parent_->learn_start();
    return;
  }

  // Navigate mode → click acts on the highlighted field. Booleans (Power,
  // Sec Flame) toggle in place — no point in an edit-then-rotate dance for
  // a two-state value. Info opens its overlay. Numeric fields enter edit
  // mode so a follow-up rotation adjusts them.
  if (this->ui_state_ == UIState::kNavigate) {
    // The cursor may have been parked on a now-disabled field (e.g. LIGHT
    // after power-off was triggered externally). Don't enter edit there —
    // skip to the next navigable field instead.
    if (!this->is_field_navigable_(this->selected_)) {
      this->cycle_selection_();
      return;
    }
    if (this->selected_ == Field::kSettings) {
      this->view_ = View::kSettings;
      this->selected_setting_ = SettingItem::kLeds;
      ESP_LOGI(TAG, "Settings → opening settings page");
      return;
    }
    if (this->selected_ == Field::kClimate) {
      this->view_ = View::kClimate;
      this->selected_climate_ = ClimateField::kMode;
      this->ui_state_ = UIState::kNavigate;
      ESP_LOGI(TAG, "Climate → opening climate editor");
      return;
    }
    if (this->selected_ == Field::kPower) {
      const bool new_state = !this->parent_->current_state_.power;
      this->parent_->set_power(new_state);
      this->parent_->queue_send();
      ESP_LOGI(TAG, "Toggle POWER → %s", new_state ? "ON" : "OFF");
      return;
    }
    if (this->selected_ == Field::kSecondary) {
      // Base the toggle on the live state when power is on, on the
      // remembered preference when power is off. set_secondary_flame
      // routes to the right destination (current vs remembered) based
      // on power state.
      const bool base =
          this->parent_->current_state_.power
              ? this->parent_->current_state_.secondary_flame
              : this->parent_->get_remembered_secondary_flame();
      this->parent_->set_secondary_flame(!base);
      this->parent_->queue_send();
      ESP_LOGI(TAG, "Toggle SEC FLAME → %s", !base ? "ON" : "OFF");
      return;
    }
    this->ui_state_ = UIState::kEdit;
    ESP_LOGD(TAG, "Edit %s", this->field_label_(this->selected_));
    return;
  }

  // Edit mode → click confirms and returns to navigate.
  this->ui_state_ = UIState::kNavigate;
  ESP_LOGD(TAG, "Confirm %s", this->field_label_(this->selected_));
}

bool ProFlame2UI::is_field_navigable_(Field f) const {
  if (this->parent_ == nullptr) {
    return true;
  }
  // LIGHT and SECONDARY are both editable even when power is off — the
  // user's choice is stashed in remembered_light_level_ /
  // remembered_secondary_flame_ and applied on the next set_power(true).
  // Letting them pre-dial avoids the "turn fireplace on then immediately
  // walk back to dial things in" friction.
  //
  // Climate field only makes sense if a climate component is wired. Hide it
  // entirely on builds that omit the climate: block (plain ESP32 builds may
  // skip it).
  if (f == Field::kClimate &&
      (this->parent_ == nullptr || this->parent_->get_climate() == nullptr)) {
    return false;
  }
  // Settings page hosts the LED toggle (and Clear Pairing, Reboot, etc.).
  // Always reachable — even on builds without a status-LED strip, users may
  // still want Clear Pairing / Reboot.
  return true;
}

void ProFlame2UI::cycle_selection_() {
  const uint8_t total = static_cast<uint8_t>(Field::kCount);
  uint8_t cur = static_cast<uint8_t>(this->selected_);
  for (uint8_t i = 0; i < total; i++) {
    cur = (cur + 1) % total;
    if (this->is_field_navigable_(static_cast<Field>(cur))) {
      this->selected_ = static_cast<Field>(cur);
      break;
    }
  }
  ESP_LOGD(TAG, "Selected field: %s", this->field_label_(this->selected_));
}

void ProFlame2UI::cycle_selection_back_() {
  const uint8_t total = static_cast<uint8_t>(Field::kCount);
  uint8_t cur = static_cast<uint8_t>(this->selected_);
  for (uint8_t i = 0; i < total; i++) {
    cur = (cur + total - 1) % total;
    if (this->is_field_navigable_(static_cast<Field>(cur))) {
      this->selected_ = static_cast<Field>(cur);
      break;
    }
  }
  ESP_LOGD(TAG, "Selected field: %s", this->field_label_(this->selected_));
}

void ProFlame2UI::apply_delta_to_selected_(int direction) {
  if (this->parent_ == nullptr) {
    return;
  }
  const auto &state = this->parent_->current_state_;

  switch (this->selected_) {
    case Field::kPower:
      // Clockwise = ON, counter-clockwise = OFF. Idempotent setters short-
      // circuit if the state already matches, so this is safe to call always.
      this->parent_->set_power(direction > 0);
      break;
    case Field::kSecondary:
      this->parent_->set_secondary_flame(direction > 0);
      break;
    case Field::kSettings:
    case Field::kClimate:
      // Click-action menu items — rotation is a no-op. Don't fall through
      // to queue_send.
      return;
    case Field::kFlame: {
      int v = static_cast<int>(state.flame_level) + direction;
      v = std::clamp(v, 0, 6);
      this->parent_->set_flame_level(static_cast<uint8_t>(v));
      break;
    }
    case Field::kFan: {
      int v = static_cast<int>(state.fan_level) + direction;
      v = std::clamp(v, 0, 6);
      this->parent_->set_fan_level(static_cast<uint8_t>(v));
      break;
    }
    case Field::kLight: {
      // When power is on, base the delta on the live level. When power is
      // off, base it on the remembered preference so the user can dial up
      // a pre-set level even though current_state_.light_level is 0.
      const int base = state.power
                           ? static_cast<int>(state.light_level)
                           : static_cast<int>(
                                 this->parent_->get_remembered_light_level());
      int v = std::clamp(base + direction, 0, 6);
      this->parent_->set_light_level(static_cast<uint8_t>(v));
      break;
    }
    default:
      return;
  }
  this->parent_->queue_send();
}

const char *ProFlame2UI::field_label_(Field f) const {
  switch (f) {
    case Field::kFlame:     return "FLAME";
    case Field::kFan:       return "FAN";
    case Field::kPower:     return "POWER";
    case Field::kSecondary: return "SEC FLAME";
    case Field::kLight:     return "LIGHT";
    case Field::kClimate:   return "CLIMATE";
    case Field::kSettings:  return "SETTINGS";
    default:                return "?";
  }
}

int ProFlame2UI::field_value_(Field f) const {
  if (this->parent_ == nullptr) {
    return 0;
  }
  const auto &state = this->parent_->current_state_;
  switch (f) {
    case Field::kFlame:     return state.flame_level;
    case Field::kFan:       return state.fan_level;
    case Field::kPower:     return state.power ? 1 : 0;
    case Field::kSecondary:
      // Same as kLight: live when power is on, remembered preference
      // when power is off. The burner is physically dark in the off
      // case, but the dial reflects the user's intent.
      return (state.power ? state.secondary_flame
                          : this->parent_->get_remembered_secondary_flame())
                 ? 1
                 : 0;
    // Show the user's intent: live level when on, remembered preference
    // when off. The bulb is physically dark when power is off, but the
    // dial reflects what the user has chosen for the next power-on.
    case Field::kLight:
      return state.power ? state.light_level
                         : this->parent_->get_remembered_light_level();
    case Field::kClimate:   return 0;
    case Field::kSettings:  return 0;
    default:                return 0;
  }
}

const char *ProFlame2UI::setting_label_(SettingItem s) const {
  switch (s) {
    case SettingItem::kLeds:             return "LEDs";
    case SettingItem::kBatteryBar:       return "BATTERY BAR";
    case SettingItem::kClockOnIdle:      return "CLOCK ON IDLE";
    case SettingItem::kBacklightTimeout: return "BACKLIGHT";
    case SettingItem::kEncoderInvert:    return "INVERT ENCODER";
    case SettingItem::kInfo:             return "DEVICE INFO";
    case SettingItem::kClearPairing:     return "CLEAR PAIRING";
    case SettingItem::kReboot:           return "REBOOT";
    case SettingItem::kBack:             return "BACK";
    default:                             return "?";
  }
}

const char *ProFlame2UI::setting_value_(SettingItem s) const {
  switch (s) {
    case SettingItem::kLeds:
      return (this->leds_switch_ != nullptr && this->leds_switch_->state)
                 ? "ON"
                 : "OFF";
    case SettingItem::kBatteryBar:
      return (this->battery_bar_switch_ == nullptr ||
              this->battery_bar_switch_->state)
                 ? "ON"
                 : "OFF";
    case SettingItem::kClockOnIdle:
      return (this->clock_on_idle_switch_ != nullptr &&
              this->clock_on_idle_switch_->state)
                 ? "ON"
                 : "OFF";
    case SettingItem::kEncoderInvert:
      return (this->encoder_invert_switch_ != nullptr &&
              this->encoder_invert_switch_->state)
                 ? "ON"
                 : "OFF";
    case SettingItem::kBacklightTimeout:
      // Surfaces the current select option ("30s", "1m", etc.) inline so
      // users don't have to enter an edit mode just to see it.
      if (this->backlight_timeout_select_ != nullptr) {
        const auto cur = this->backlight_timeout_select_->current_option();
        if (!cur.empty()) {
          // Static buffer is fine — single-threaded display path.
          static char buf[8];
          std::snprintf(buf, sizeof(buf), "%s", cur.c_str());
          return buf;
        }
      }
      return "30s";
    case SettingItem::kInfo:
    case SettingItem::kClearPairing:
    case SettingItem::kReboot:
    case SettingItem::kBack:
    default:
      return ">>";
  }
}

void ProFlame2UI::draw(display::Display &it) {
  if (this->parent_ == nullptr) {
    return;
  }

  const int width = it.get_width();
  const int height = it.get_height();

  // When a learn flow is active, the UI is fully dedicated to it — the
  // normal idle screen would just be confusing during pairing.
  if (this->parent_->get_learn_state() !=
      ProFlame2Component::LearnState::kIdle) {
    this->draw_learn_(it, width, height);
    return;
  }
  // Clock screensaver wins over kIdle / kInfo / kSettings — once the user is
  // idle past the backlight timeout, we always show time (when enabled).
  if (this->is_clock_screensaver_active_()) {
    this->draw_clock_(it, width, height);
    return;
  }
  if (this->view_ == View::kInfo) {
    this->draw_info_(it, width, height);
    return;
  }
  if (this->view_ == View::kSettings) {
    this->draw_settings_(it, width, height);
    return;
  }
  if (this->view_ == View::kClimate) {
    this->draw_climate_(it, width, height);
    return;
  }
  this->draw_idle_(it, width, height);
}

void ProFlame2UI::draw_status_bar_(display::Display &it, int width) {
  if (this->font_small_ == nullptr) {
    return;
  }

  it.print(4, 2, this->font_small_, kWhite, "ProFlame");

  // Battery indicator — graphical bar by default (gated on
  // battery_bar_switch_), text fallback when off / unset. Color tier follows
  // charge level.
  const bool battery_known =
      this->battery_sensor_ != nullptr &&
      !std::isnan(this->battery_sensor_->state);
  int pct = 0;
  if (battery_known) {
    pct = static_cast<int>(std::lround(this->battery_sensor_->state));
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
  }
  Color bat_color = battery_known ? battery_color_for_pct(pct) : kDim;

  const bool bar_mode =
      this->battery_bar_switch_ == nullptr ||
      this->battery_bar_switch_->state;

  if (bar_mode && battery_known) {
    // Battery icon: 28-wide body + 3-wide tip, centered horizontally near the
    // top of the status bar. The body fills proportional to charge.
    constexpr int kBarBodyW = 28;
    constexpr int kBarH = 11;
    constexpr int kBarTipW = 3;
    constexpr int kBarTipH = 5;
    constexpr int kBarY = 3;

    const int total_w = kBarBodyW + kBarTipW;
    const int x0 = width / 2 - total_w / 2;

    // Outline.
    it.rectangle(x0, kBarY, kBarBodyW, kBarH, bat_color);
    // Tip on the right.
    const int tip_y = kBarY + (kBarH - kBarTipH) / 2;
    it.filled_rectangle(x0 + kBarBodyW, tip_y, kBarTipW, kBarTipH, bat_color);

    // Fill: 1px inset from the outline so the rim stays visible.
    const int inner_w = kBarBodyW - 2;
    int fill_w = (inner_w * pct) / 100;
    if (fill_w < 0) fill_w = 0;
    if (fill_w > inner_w) fill_w = inner_w;
    if (fill_w > 0) {
      it.filled_rectangle(x0 + 1, kBarY + 1, fill_w, kBarH - 2, bat_color);
    }
  } else {
    char bat_buf[16];
    if (battery_known) {
      std::snprintf(bat_buf, sizeof(bat_buf), "BAT %d%%", pct);
    } else {
      std::snprintf(bat_buf, sizeof(bat_buf), "BAT --");
    }
    it.print(width / 2, 2, this->font_small_, bat_color,
             display::TextAlign::TOP_CENTER, bat_buf);
  }

  const bool ha_known = (this->status_sensor_ != nullptr);
  const bool ha_ok = ha_known && this->status_sensor_->state;
  const char *ha_text = ha_known ? (ha_ok ? "HA: connected" : "HA: offline")
                                 : "HA: ?";
  Color ha_color = ha_known ? (ha_ok ? kGreen : kRed) : kDim;
  it.printf(width - 4, 2, this->font_small_, ha_color,
            display::TextAlign::TOP_RIGHT, "%s", ha_text);

  it.line(0, 18, width, 18, kDim);
}

void ProFlame2UI::on_settings_click_() {
  switch (this->selected_setting_) {
    case SettingItem::kLeds:
      // Toggle the HA-visible "Status LEDs Enabled" template switch. Same
      // single-source-of-truth wiring as the previous in-cog toggle.
      if (this->leds_switch_ != nullptr) {
        const bool new_state = !this->leds_switch_->state;
        if (new_state) {
          this->leds_switch_->turn_on();
        } else {
          this->leds_switch_->turn_off();
        }
        ESP_LOGI(TAG, "Settings: STATUS LEDS → %s", new_state ? "ON" : "OFF");
      }
      return;
    case SettingItem::kBatteryBar:
      if (this->battery_bar_switch_ != nullptr) {
        const bool new_state = !this->battery_bar_switch_->state;
        if (new_state) {
          this->battery_bar_switch_->turn_on();
        } else {
          this->battery_bar_switch_->turn_off();
        }
        ESP_LOGI(TAG, "Settings: BATTERY BAR → %s", new_state ? "ON" : "OFF");
      }
      return;
    case SettingItem::kClockOnIdle:
      if (this->clock_on_idle_switch_ != nullptr) {
        const bool new_state = !this->clock_on_idle_switch_->state;
        if (new_state) {
          this->clock_on_idle_switch_->turn_on();
        } else {
          this->clock_on_idle_switch_->turn_off();
        }
        ESP_LOGI(TAG, "Settings: CLOCK ON IDLE → %s", new_state ? "ON" : "OFF");
      }
      return;
    case SettingItem::kEncoderInvert:
      if (this->encoder_invert_switch_ != nullptr) {
        const bool new_state = !this->encoder_invert_switch_->state;
        if (new_state) {
          this->encoder_invert_switch_->turn_on();
        } else {
          this->encoder_invert_switch_->turn_off();
        }
        ESP_LOGI(TAG, "Settings: INVERT ENCODER → %s", new_state ? "ON" : "OFF");
      }
      return;
    case SettingItem::kBacklightTimeout:
      // Cycle through the select's options on each click. Wraps at the end.
      if (this->backlight_timeout_select_ != nullptr) {
        const auto &opts = this->backlight_timeout_select_->traits.get_options();
        if (!opts.empty()) {
          const auto cur = this->backlight_timeout_select_->current_option();
          size_t idx = 0;
          for (size_t i = 0; i < opts.size(); i++) {
            if (opts[i] != nullptr && !cur.empty() &&
                std::strcmp(cur.c_str(), opts[i]) == 0) {
              idx = (i + 1) % opts.size();
              break;
            }
          }
          auto call = this->backlight_timeout_select_->make_call();
          call.set_option(std::string{opts[idx]});
          call.perform();
          ESP_LOGI(TAG, "Settings: BACKLIGHT → %s", opts[idx]);
        }
      }
      return;
    case SettingItem::kInfo:
      this->view_ = View::kInfo;
      ESP_LOGI(TAG, "Settings: opening device info");
      return;
    case SettingItem::kClearPairing:
      ESP_LOGI(TAG, "Settings: clearing pairing → reboot");
      // Component handles NVS-invalidate + safe_reboot internally.
      this->parent_->clear_learned_state();
      return;
    case SettingItem::kReboot:
      ESP_LOGI(TAG, "Settings: rebooting");
      App.safe_reboot();
      return;
    case SettingItem::kBack:
      this->view_ = View::kIdle;
      return;
    default:
      return;
  }
}

void ProFlame2UI::on_settings_rotate_(int direction) {
  const int total = static_cast<int>(SettingItem::kCount);
  int cur = static_cast<int>(this->selected_setting_);
  cur = (cur + direction + total) % total;
  this->selected_setting_ = static_cast<SettingItem>(cur);
  ESP_LOGD(TAG, "Settings cursor: %s",
           this->setting_label_(this->selected_setting_));
}

const char *ProFlame2UI::climate_field_label_(ClimateField c) {
  switch (c) {
    case ClimateField::kMode:          return "MODE";
    case ClimateField::kTargetTemp:    return "TARGET";
    case ClimateField::kFanMode:       return "FAN";
    case ClimateField::kHeatFlame:     return "HEAT FLAME";
    case ClimateField::kHeatFan:       return "HEAT FAN";
    case ClimateField::kHeatLight:     return "HEAT LIGHT";
    case ClimateField::kHeatSecondary: return "HEAT SEC";
    case ClimateField::kBack:          return "BACK";
    default:                           return "?";
  }
}

const char *ProFlame2UI::climate_mode_label_(climate::ClimateMode m) {
  switch (m) {
    case climate::CLIMATE_MODE_OFF:  return "OFF";
    case climate::CLIMATE_MODE_HEAT: return "HEAT";
    default:                         return "?";
  }
}

const char *ProFlame2UI::climate_fan_label_(climate::ClimateFanMode f) {
  switch (f) {
    case climate::CLIMATE_FAN_OFF:    return "OFF";
    case climate::CLIMATE_FAN_LOW:    return "LOW";
    case climate::CLIMATE_FAN_MEDIUM: return "MED";
    case climate::CLIMATE_FAN_HIGH:   return "HIGH";
    default:                          return "?";
  }
}

namespace {
// Fahrenheit display for parity with the YAML's visual block (60°F / 85°F).
// Internal climate state stays Celsius — convert at the edge only.
int c_to_f_round(float c) {
  return static_cast<int>(std::lround(c * 9.0f / 5.0f + 32.0f));
}
}  // namespace

void ProFlame2UI::on_climate_click_() {
  if (this->parent_ == nullptr) {
    return;
  }
  auto *clim = this->parent_->get_climate();
  if (clim == nullptr) {
    this->view_ = View::kIdle;
    return;
  }

  switch (this->selected_climate_) {
    case ClimateField::kMode: {
      // Toggle HEAT ↔ OFF.
      const auto next = (clim->mode == climate::CLIMATE_MODE_HEAT)
                            ? climate::CLIMATE_MODE_OFF
                            : climate::CLIMATE_MODE_HEAT;
      auto call = clim->make_call();
      call.set_mode(next);
      call.perform();
      ESP_LOGI(TAG, "Climate: MODE → %s", this->climate_mode_label_(next));
      return;
    }
    case ClimateField::kTargetTemp:
      // Toggle edit mode. Rotation in kEdit adjusts the target by 0.5 °C
      // (≈ 1 °F) per detent; click again confirms back to navigate.
      this->ui_state_ = (this->ui_state_ == UIState::kEdit)
                            ? UIState::kNavigate
                            : UIState::kEdit;
      ESP_LOGD(TAG, "Climate: %s target temp",
               this->ui_state_ == UIState::kEdit ? "editing" : "confirming");
      return;
    case ClimateField::kFanMode: {
      // Cycle OFF → LOW → MED → HIGH → OFF.
      climate::ClimateFanMode next = climate::CLIMATE_FAN_OFF;
      const auto cur =
          clim->fan_mode.value_or(climate::CLIMATE_FAN_OFF);
      switch (cur) {
        case climate::CLIMATE_FAN_OFF:    next = climate::CLIMATE_FAN_LOW;    break;
        case climate::CLIMATE_FAN_LOW:    next = climate::CLIMATE_FAN_MEDIUM; break;
        case climate::CLIMATE_FAN_MEDIUM: next = climate::CLIMATE_FAN_HIGH;   break;
        case climate::CLIMATE_FAN_HIGH:   next = climate::CLIMATE_FAN_OFF;    break;
        default:                          next = climate::CLIMATE_FAN_OFF;    break;
      }
      auto call = clim->make_call();
      call.set_fan_mode(next);
      call.perform();
      ESP_LOGI(TAG, "Climate: FAN → %s", this->climate_fan_label_(next));
      return;
    }
    // Heat-mode config rows. The three numeric ones use the same
    // click-to-edit pattern as target temp (kEdit toggles, rotation
    // adjusts ±1). The secondary-flame switch toggles directly.
    case ClimateField::kHeatFlame:
    case ClimateField::kHeatFan:
    case ClimateField::kHeatLight:
      this->ui_state_ = (this->ui_state_ == UIState::kEdit)
                            ? UIState::kNavigate
                            : UIState::kEdit;
      ESP_LOGD(TAG, "Climate: %s %s",
               this->ui_state_ == UIState::kEdit ? "editing" : "confirming",
               this->climate_field_label_(this->selected_climate_));
      return;
    case ClimateField::kHeatSecondary: {
      auto *sw = clim->get_heat_secondary_flame();
      if (sw == nullptr) {
        return;
      }
      const bool next = !sw->state;
      if (next) {
        sw->turn_on();
      } else {
        sw->turn_off();
      }
      ESP_LOGI(TAG, "Climate: HEAT SEC → %s", next ? "ON" : "OFF");
      return;
    }
    case ClimateField::kBack:
      this->ui_state_ = UIState::kNavigate;
      this->view_ = View::kIdle;
      return;
    default:
      return;
  }
}

void ProFlame2UI::on_climate_rotate_(int direction) {
  if (this->parent_ == nullptr) {
    return;
  }
  auto *clim = this->parent_->get_climate();
  if (clim == nullptr) {
    return;
  }

  // Edit mode — adjust the value of the currently-selected row rather than
  // moving the cursor. Each numeric row clamps to its own min/max.
  if (this->ui_state_ == UIState::kEdit) {
    switch (this->selected_climate_) {
      case ClimateField::kTargetTemp: {
        const auto t = clim->traits();
        const float min_c = t.get_visual_min_temperature();
        const float max_c = t.get_visual_max_temperature();
        const float step = t.get_visual_target_temperature_step();
        float next = clim->target_temperature + (direction > 0 ? step : -step);
        if (next < min_c) next = min_c;
        if (next > max_c) next = max_c;
        auto call = clim->make_call();
        call.set_target_temperature(next);
        call.perform();
        return;
      }
      case ClimateField::kHeatFlame:
      case ClimateField::kHeatFan:
      case ClimateField::kHeatLight: {
        // Pull the appropriate config number, ±1 within its own bounds.
        // HEAT FLAME's min is 1 (the fireplace expects a non-zero flame
        // when burning); HEAT FAN/LIGHT can go to 0 ("off").
        number::Number *num = nullptr;
        int min_v = 0;
        switch (this->selected_climate_) {
          case ClimateField::kHeatFlame:
            num = clim->get_heat_flame_level();
            min_v = 1;
            break;
          case ClimateField::kHeatFan:
            num = clim->get_heat_fan_level();
            min_v = 0;
            break;
          case ClimateField::kHeatLight:
            num = clim->get_heat_light_level();
            min_v = 0;
            break;
          default: break;
        }
        if (num == nullptr) {
          return;
        }
        const float cur_f = num->has_state() ? num->state : static_cast<float>(min_v);
        int next = static_cast<int>(cur_f) + direction;
        if (next < min_v) next = min_v;
        if (next > 6) next = 6;
        num->make_call().set_value(static_cast<float>(next)).perform();
        return;
      }
      default:
        // Other rows have no edit mode; fall through to navigate.
        break;
    }
  }

  // Navigate mode — cycle through climate fields. Wrap in either direction.
  const int total = static_cast<int>(ClimateField::kCount);
  int cur = static_cast<int>(this->selected_climate_);
  cur = (cur + direction + total) % total;
  this->selected_climate_ = static_cast<ClimateField>(cur);
  ESP_LOGD(TAG, "Climate cursor: %s",
           this->climate_field_label_(this->selected_climate_));
}

void ProFlame2UI::draw_climate_(display::Display &it, int width, int height) {
  this->draw_status_bar_(it, width);

  if (this->parent_ == nullptr || this->font_small_ == nullptr) {
    return;
  }
  auto *clim = this->parent_->get_climate();
  if (clim == nullptr) {
    return;
  }

  // Eight rows on a 170 px panel — same layout shape as the settings page.
  // font_small uniformly; selection signaled by cursor + accent color, no
  // bigger font on the active row (which would push siblings into each other).
  const int top = 22;
  const int bottom_reserve = 6;
  const int rows = static_cast<int>(ClimateField::kCount);
  const int row_h = (height - top - bottom_reserve) / rows;

  for (int i = 0; i < rows; i++) {
    const ClimateField c = static_cast<ClimateField>(i);
    const bool selected = (c == this->selected_climate_);
    const bool editing = selected && this->ui_state_ == UIState::kEdit;
    const int center_y = top + i * row_h + row_h / 2;

    if (selected) {
      it.print(4, center_y, this->font_small_, kAccent,
               display::TextAlign::CENTER_LEFT, ">");
    }
    Color label_color = selected ? kAccent : kDim;
    it.print(24, center_y, this->font_small_, label_color,
             display::TextAlign::CENTER_LEFT, this->climate_field_label_(c));

    // Right-side value column, per-field formatting.
    char buf[16];
    Color value_color = kWhite;
    switch (c) {
      case ClimateField::kMode: {
        // "OFF (72°F)" — show the room-temp inline so the user can see the
        // current reading without a dedicated header row, since climate's
        // sole context is "how does current compare to target?".
        const char *mode = this->climate_mode_label_(clim->mode);
        if (!std::isnan(clim->current_temperature)) {
          std::snprintf(buf, sizeof(buf), "%s %d°F", mode,
                        c_to_f_round(clim->current_temperature));
        } else {
          std::snprintf(buf, sizeof(buf), "%s", mode);
        }
        value_color = (clim->mode == climate::CLIMATE_MODE_HEAT) ? kGreen : kDim;
        break;
      }
      case ClimateField::kTargetTemp: {
        const int f = c_to_f_round(clim->target_temperature);
        if (editing) {
          std::snprintf(buf, sizeof(buf), "[%d°F]", f);
          value_color = kAccent;
        } else {
          std::snprintf(buf, sizeof(buf), "%d°F", f);
        }
        break;
      }
      case ClimateField::kFanMode: {
        const auto m = clim->fan_mode.value_or(climate::CLIMATE_FAN_OFF);
        std::snprintf(buf, sizeof(buf), "%s", this->climate_fan_label_(m));
        value_color = (m == climate::CLIMATE_FAN_OFF) ? kDim : kWhite;
        break;
      }
      case ClimateField::kHeatFlame:
      case ClimateField::kHeatFan:
      case ClimateField::kHeatLight: {
        number::Number *num = nullptr;
        switch (c) {
          case ClimateField::kHeatFlame: num = clim->get_heat_flame_level(); break;
          case ClimateField::kHeatFan:   num = clim->get_heat_fan_level();   break;
          case ClimateField::kHeatLight: num = clim->get_heat_light_level(); break;
          default: break;
        }
        if (num == nullptr) {
          std::snprintf(buf, sizeof(buf), "--");
          value_color = kDim;
        } else {
          const int v = num->has_state() ? static_cast<int>(num->state) : 0;
          if (editing) {
            std::snprintf(buf, sizeof(buf), "[%d]", v);
            value_color = kAccent;
          } else {
            std::snprintf(buf, sizeof(buf), "%d", v);
            value_color = (v == 0) ? kDim : kWhite;
          }
        }
        break;
      }
      case ClimateField::kHeatSecondary: {
        auto *sw = clim->get_heat_secondary_flame();
        const bool on = (sw != nullptr && sw->state);
        std::snprintf(buf, sizeof(buf), "%s", on ? "ON" : "OFF");
        value_color = on ? kGreen : kDim;
        break;
      }
      case ClimateField::kBack:
        std::snprintf(buf, sizeof(buf), ">>");
        value_color = selected ? kAccent : kDim;
        break;
      default:
        std::snprintf(buf, sizeof(buf), "?");
        break;
    }
    it.printf(width - 8, center_y, this->font_small_, value_color,
              display::TextAlign::CENTER_RIGHT, "%s", buf);
  }
}

void ProFlame2UI::draw_clock_(display::Display &it, int width, int height) {
  if (this->time_ == nullptr) {
    return;
  }
  const auto now = this->time_->now();
  if (!now.is_valid()) {
    // Time hasn't synced yet — fall back to the normal idle screen so the
    // user isn't staring at "--:--" until HA sends a clock packet.
    this->draw_idle_(it, width, height);
    return;
  }

  // Big HH:MM centered. Date below in font_medium for orientation.
  char hh_mm[8];
  std::snprintf(hh_mm, sizeof(hh_mm), "%02u:%02u",
                static_cast<unsigned>(now.hour),
                static_cast<unsigned>(now.minute));

  if (this->font_large_ != nullptr) {
    it.print(width / 2, height / 2 - 18, this->font_large_, kAccent,
             display::TextAlign::CENTER, hh_mm);
  } else if (this->font_medium_ != nullptr) {
    it.print(width / 2, height / 2 - 18, this->font_medium_, kAccent,
             display::TextAlign::CENTER, hh_mm);
  }

  // ESPTime's day_of_week is 1..7 starting Sunday; format builds a short
  // "Wed Apr 29" line below the time so the page isn't info-free if the
  // user glances over.
  static const char *const kDow[] = {"", "Sun", "Mon", "Tue", "Wed",
                                     "Thu", "Fri", "Sat"};
  static const char *const kMon[] = {"",    "Jan", "Feb", "Mar", "Apr",
                                     "May", "Jun", "Jul", "Aug", "Sep",
                                     "Oct", "Nov", "Dec"};
  char date_buf[24];
  const unsigned dow = now.day_of_week;
  const unsigned month = now.month;
  std::snprintf(date_buf, sizeof(date_buf), "%s %s %u",
                (dow >= 1 && dow <= 7) ? kDow[dow] : "",
                (month >= 1 && month <= 12) ? kMon[month] : "",
                static_cast<unsigned>(now.day_of_month));
  if (this->font_medium_ != nullptr) {
    it.print(width / 2, height / 2 + 30, this->font_medium_, kDim,
             display::TextAlign::CENTER, date_buf);
  } else if (this->font_small_ != nullptr) {
    it.print(width / 2, height / 2 + 30, this->font_small_, kDim,
             display::TextAlign::CENTER, date_buf);
  }
}

void ProFlame2UI::draw_settings_(display::Display &it, int width, int height) {
  this->draw_status_bar_(it, width);

  // 9 items don't fit at the same row height as the main page (which uses
  // font_medium for the selected row). We use font_small uniformly here and
  // signal selection via cursor + color only — keeps every row at ~14 px.
  // No title / hint rows; the BACK item at the bottom is the exit affordance.
  const int top = 22;
  const int bottom_reserve = 8;
  const int rows = static_cast<int>(SettingItem::kCount);
  const int row_h = (height - top - bottom_reserve) / rows;

  if (this->font_small_ == nullptr) {
    return;
  }
  font::Font *text_font = this->font_small_;

  for (int i = 0; i < rows; i++) {
    const SettingItem s = static_cast<SettingItem>(i);
    const bool selected = (s == this->selected_setting_);
    const int center_y = top + i * row_h + row_h / 2;

    if (selected) {
      it.print(4, center_y, text_font, kAccent,
               display::TextAlign::CENTER_LEFT, ">");
    }
    const Color label_color = selected ? kAccent : kDim;
    it.print(24, center_y, text_font, label_color,
             display::TextAlign::CENTER_LEFT, this->setting_label_(s));

    // Value column. ON/OFF for toggles tints green/dim, action arrows tint
    // accent on the selected row and dim otherwise.
    const char *value = this->setting_value_(s);
    Color value_color = kDim;
    if (s == SettingItem::kLeds) {
      const bool on = (this->leds_switch_ != nullptr && this->leds_switch_->state);
      value_color = on ? kGreen : kDim;
    } else if (s == SettingItem::kBatteryBar) {
      const bool on = (this->battery_bar_switch_ == nullptr ||
                       this->battery_bar_switch_->state);
      value_color = on ? kGreen : kDim;
    } else if (s == SettingItem::kClockOnIdle) {
      const bool on = (this->clock_on_idle_switch_ != nullptr &&
                       this->clock_on_idle_switch_->state);
      value_color = on ? kGreen : kDim;
    } else if (s == SettingItem::kEncoderInvert) {
      const bool on = (this->encoder_invert_switch_ != nullptr &&
                       this->encoder_invert_switch_->state);
      value_color = on ? kGreen : kDim;
    } else if (s == SettingItem::kBacklightTimeout) {
      // Always rendered in white so the value reads as text rather than a
      // toggle. Selection still tints the label kAccent above.
      value_color = kWhite;
    } else {
      value_color = selected ? kAccent : kDim;
    }
    it.printf(width - 8, center_y, text_font, value_color,
              display::TextAlign::CENTER_RIGHT, "%s", value);
  }

}

void ProFlame2UI::draw_idle_(display::Display &it, int width, int height) {
  this->draw_status_bar_(it, width);

  // Field rows. Selected row uses font_medium, non-selected uses font_small —
  // big enough to show the active selection clearly without overflowing into
  // adjacent rows. Each row is centered vertically so size differences don't
  // shift the baseline.
  const int top = 22;
  const int bottom_reserve = 14;
  const int rows = static_cast<int>(Field::kCount);
  const int row_h = (height - top - bottom_reserve) / rows;

  for (int i = 0; i < rows; i++) {
    const Field f = static_cast<Field>(i);
    const bool selected = (f == this->selected_);
    const int center_y = top + i * row_h + row_h / 2;

    font::Font *text_font = selected ? this->font_medium_ : this->font_small_;
    if (text_font == nullptr) {
      // Fall back to whichever font is available so we still render something.
      text_font = this->font_small_ != nullptr ? this->font_small_
                                               : this->font_medium_;
    }
    if (text_font == nullptr) {
      continue;
    }

    // Disabled rows (e.g. LIGHT when power is off) render in extra-dim grey
    // so the user can see they exist but aren't reachable. The cursor skips
    // them in the cycle.
    const bool navigable = this->is_field_navigable_(f);
    const Color disabled = Color(0x40, 0x40, 0x40);
    const Color label_color = !navigable ? disabled
                                          : selected ? kAccent
                                                     : kDim;

    if (selected && navigable) {
      it.print(4, center_y, text_font, kAccent,
               display::TextAlign::CENTER_LEFT, ">");
    }

    it.print(24, center_y, text_font, label_color,
             display::TextAlign::CENTER_LEFT, this->field_label_(f));

    // Value text + per-field color cue. ON states light up green; flame level
    // gets a red→orange→amber tint to map naturally to the burner intensity.
    // Other numeric values stay white to avoid visual noise. In edit mode the
    // selected field's value is wrapped in [ ] and tinted orange so the user
    // gets a clear "rotate to change" cue.
    const bool editing = selected && this->ui_state_ == UIState::kEdit;

    char raw_buf[12];
    Color value_color = kWhite;
    const int v = this->field_value_(f);
    switch (f) {
      case Field::kSettings:
      case Field::kClimate:
        std::snprintf(raw_buf, sizeof(raw_buf), ">>");
        value_color = selected ? kAccent : kDim;
        break;
      case Field::kPower:
      case Field::kSecondary:
        std::snprintf(raw_buf, sizeof(raw_buf), "%s", v ? "ON" : "OFF");
        value_color = v ? kGreen : kDim;
        break;
      case Field::kFlame:
        std::snprintf(raw_buf, sizeof(raw_buf), "%d", v);
        value_color = flame_color_for_level(v);
        break;
      default:
        std::snprintf(raw_buf, sizeof(raw_buf), "%d", v);
        value_color = (v == 0) ? kDim : kWhite;
        break;
    }

    char buf[16];
    if (editing) {
      std::snprintf(buf, sizeof(buf), "[%s]", raw_buf);
      value_color = kAccent;
    } else {
      std::snprintf(buf, sizeof(buf), "%s", raw_buf);
    }
    if (!navigable) {
      value_color = disabled;
    }

    it.printf(width - 8, center_y, text_font, value_color,
              display::TextAlign::CENTER_RIGHT, "%s", buf);
  }
}

void ProFlame2UI::draw_info_(display::Display &it, int width, int height) {
  this->draw_status_bar_(it, width);

  if (this->font_small_ == nullptr) {
    return;
  }

  // 8 lines of body content, ~16 px each. Status bar = 0..18, body = 22..150,
  // footer hint = ~158. Labels stay dim so the values pop in white/accent.
  int y = 24;
  const int line_h = 16;
  const int label_x = 8;
  const int value_x = 64;

  auto draw_row = [&](const char *label, const char *value, Color value_color) {
    it.print(label_x, y, this->font_small_, kDim, label);
    it.print(value_x, y, this->font_small_, value_color, value);
    y += line_h;
  };

  // IP / WiFi state. wifi::global_wifi_component is always non-null when the
  // wifi: block is present in YAML, but guard anyway to be defensive. Use the
  // modern fixed-buffer APIs (str_to / wifi_ssid_to) — the std::string
  // returning variants are deprecated and slated for removal.
  if (wifi::global_wifi_component != nullptr &&
      wifi::global_wifi_component->is_connected()) {
    char ip_buf[network::IP_ADDRESS_BUFFER_SIZE] = {};
    const auto ips = wifi::global_wifi_component->wifi_sta_ip_addresses();
    bool found = false;
    for (const auto &ip : ips) {
      if (ip.is_set() && ip.is_ip4()) {
        ip.str_to(ip_buf);
        found = true;
        break;
      }
    }
    if (!found) {
      std::strcpy(ip_buf, "(no ip)");
    }
    draw_row("IP:", ip_buf, kWhite);

    char ssid_buf[wifi::SSID_BUFFER_SIZE] = {};
    wifi::global_wifi_component->wifi_ssid_to(ssid_buf);
    const int rssi = wifi::global_wifi_component->wifi_rssi();
    char wifi_buf[64];
    std::snprintf(wifi_buf, sizeof(wifi_buf), "%s (%d dBm)", ssid_buf, rssi);
    draw_row("WiFi:", wifi_buf, kWhite);
  } else {
    draw_row("IP:", "(no wifi)", kRed);
    draw_row("WiFi:", "disconnected", kRed);
  }

  const bool ha_known = (this->status_sensor_ != nullptr);
  const bool ha_ok = ha_known && this->status_sensor_->state;
  Color ha_color = ha_known ? (ha_ok ? kGreen : kRed) : kDim;
  draw_row("HA:", ha_known ? (ha_ok ? "connected" : "offline") : "unknown",
           ha_color);

  const auto src = this->parent_->get_config_source();
  const bool learned =
      src == ProFlame2Component::ConfigSource::kNvsLearned;
  draw_row("Pair:", learned ? "NVS (learned)" : "YAML",
           learned ? kGreen : kAmber);

  char ser_buf[16];
  std::snprintf(ser_buf, sizeof(ser_buf), "0x%06X",
                this->parent_->get_serial_number());
  draw_row("Ser:", ser_buf, kAccent);

  uint8_t c1 = 0, d1 = 0, c2 = 0, d2 = 0;
  this->parent_->get_ecc(c1, d1, c2, d2);
  char ecc_buf[32];
  std::snprintf(ecc_buf, sizeof(ecc_buf), "c1=%X d1=%X c2=%X d2=%X",
                c1, d1, c2, d2);
  draw_row("ECC:", ecc_buf, kAccent);

  const uint32_t up_s = millis() / 1000;
  const uint32_t up_d = up_s / 86400;
  const uint32_t up_h = (up_s % 86400) / 3600;
  const uint32_t up_m = (up_s % 3600) / 60;
  char up_buf[32];
  std::snprintf(up_buf, sizeof(up_buf), "%ud %02uh %02um", up_d, up_h, up_m);
  draw_row("Up:", up_buf, kWhite);

  char build_buf[Application::BUILD_TIME_STR_SIZE] = {};
  App.get_build_time_string(build_buf);
  char fw_buf[64];
  std::snprintf(fw_buf, sizeof(fw_buf), "%s (%s)", ESPHOME_VERSION, build_buf);
  draw_row("FW:", fw_buf, kWhite);

  // Footer hint — click or rotate to exit (no more double-click).
  it.print(width / 2, height - 14, this->font_small_, kDim,
           display::TextAlign::TOP_CENTER, "click or rotate to exit");
}

void ProFlame2UI::draw_learn_(display::Display &it, int width, int height) {
  using LearnState = ProFlame2Component::LearnState;
  const auto white = Color(0xFF, 0xFF, 0xFF);
  const auto green = Color(0x40, 0xC0, 0x40);
  const auto amber = Color(0xFF, 0xB0, 0x00);
  const auto red = Color(0xFF, 0x60, 0x60);

  if (this->font_small_ != nullptr) {
    it.print(4, 2, this->font_small_, white, "PAIRING");
  }
  it.line(0, 18, width, 18, white);

  const auto state = this->parent_->get_learn_state();
  const auto &cand = this->parent_->get_learn_candidate();

  const char *headline = "";
  Color headline_color = white;
  // While capturing, the headline tells the user *which axis* still needs a
  // press — convergence requires diversity on both cmd1 and cmd2 so that the
  // inversion formula is actually validated against varying input.
  const bool need_more_cmd1 =
      cand.cmd1.distinct < ProFlame2Component::kLearnMinDistinctCmds;
  const bool need_more_cmd2 =
      cand.cmd2.distinct < ProFlame2Component::kLearnMinDistinctCmds;
  switch (state) {
    case LearnState::kListening:
      headline = "Press a button on the OEM remote";
      headline_color = white;
      break;
    case LearnState::kCapturing:
      if (need_more_cmd1 && need_more_cmd2) {
        headline = "Press more buttons (power AND flame)";
      } else if (need_more_cmd1) {
        headline = "Press POWER / LIGHT / PILOT";
      } else if (need_more_cmd2) {
        headline = "Press FLAME / FAN / SEC";
      } else {
        headline = "Capturing...";
      }
      headline_color = amber;
      break;
    case LearnState::kConverged:
      headline = "Hold button to confirm";
      headline_color = green;
      break;
    case LearnState::kPersisted:
      headline = "Saved.";
      headline_color = green;
      break;
    case LearnState::kFailed:
      headline = "Pairing failed. Press to retry.";
      headline_color = red;
      break;
    default:
      headline = "";
  }
  if (this->font_medium_ != nullptr) {
    it.print(8, 28, this->font_medium_, headline_color, headline);
  }

  // Detail block: candidate values once we have any.
  if (this->font_small_ != nullptr &&
      cand.valid_packet_count > 0) {
    char line[64];
    std::snprintf(line, sizeof(line), "serial: 0x%06X", cand.serial);
    it.print(8, 64, this->font_small_, white, line);
    std::snprintf(line, sizeof(line), "ECC: %X %X %X %X",
                  cand.c1, cand.d1, cand.c2, cand.d2);
    it.print(8, 84, this->font_small_, white, line);
    std::snprintf(line, sizeof(line),
                  "packets %u  cmd1 %u/%u  cmd2 %u/%u",
                  cand.valid_packet_count, cand.cmd1.distinct,
                  ProFlame2Component::kLearnMinDistinctCmds,
                  cand.cmd2.distinct,
                  ProFlame2Component::kLearnMinDistinctCmds);
    it.print(8, 104, this->font_small_, white, line);
  }

  // Footer hint.
  if (this->font_small_ != nullptr) {
    const char *hint = "";
    switch (state) {
      case LearnState::kListening:
      case LearnState::kCapturing:
        hint = "press = cancel";
        break;
      case LearnState::kConverged:
        hint = "press = cancel  |  hold = confirm";
        break;
      case LearnState::kFailed:
        hint = "press = back to idle";
        break;
      default:
        hint = "";
    }
    it.print(width / 2, height - 14, this->font_small_, white,
             display::TextAlign::TOP_CENTER, hint);
  }
}

}  // namespace proflame2
}  // namespace esphome

#endif  // USE_DISPLAY
