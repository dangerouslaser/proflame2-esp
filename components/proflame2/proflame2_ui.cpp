#include "proflame2_ui.h"
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
      const int direction = (v > old) ? +1 : -1;
      this->last_encoder_value_ = v;
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
  // Reserved for future timeout-driven behavior (backlight dim after 30 s).
  // Intentionally empty — the new click model is fully event-driven.
}

void ProFlame2UI::on_encoder_delta_(int direction) {
  this->last_interaction_ms_ = millis();
  // Encoder rotation has no role in learn-mode (start/confirm/cancel are all
  // button-driven). Drop deltas while a learn flow is active so accidental
  // bumps don't mutate the underlying control state mid-pairing.
  if (this->parent_ != nullptr &&
      this->parent_->get_learn_state() !=
          ProFlame2Component::LearnState::kIdle) {
    return;
  }
  // Any rotation dismisses the info screen and returns to idle.
  if (this->show_info_screen_) {
    this->show_info_screen_ = false;
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

  // Info screen: any short click dismisses it (treats the second click as
  // "confirm/exit"). Long-press is silenced here so users can't accidentally
  // drop into learn-mode from inside info.
  if (this->show_info_screen_) {
    if (!long_press) {
      this->show_info_screen_ = false;
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
    if (this->selected_ == Field::kInfo) {
      this->show_info_screen_ = true;
      this->cycle_selection_();
      ESP_LOGI(TAG, "Info menu → opening info screen");
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
      const bool new_state = !this->parent_->current_state_.secondary_flame;
      this->parent_->set_secondary_flame(new_state);
      this->parent_->queue_send();
      ESP_LOGI(TAG, "Toggle SEC FLAME → %s", new_state ? "ON" : "OFF");
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
  // Light + secondary flame are hardware-gated by the burner — when power
  // is off, skip them in the navigation cycle entirely. POWER and INFO are
  // always reachable so the cycle never starves.
  if (!this->parent_->current_state_.power &&
      (f == Field::kLight || f == Field::kSecondary)) {
    return false;
  }
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
    case Field::kInfo:
      // Info is a click-action menu item — rotation is a no-op. Don't fall
      // through to queue_send.
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
      int v = static_cast<int>(state.light_level) + direction;
      v = std::clamp(v, 0, 6);
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
    case Field::kInfo:      return "INFO";
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
    case Field::kSecondary: return state.secondary_flame ? 1 : 0;
    case Field::kLight:     return state.light_level;
    case Field::kInfo:      return 0;
    default:                return 0;
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
  if (this->show_info_screen_) {
    this->draw_info_(it, width, height);
    return;
  }
  this->draw_idle_(it, width, height);
}

void ProFlame2UI::draw_status_bar_(display::Display &it, int width) {
  if (this->font_small_ == nullptr) {
    return;
  }

  it.print(4, 2, this->font_small_, kWhite, "ProFlame");

  // Battery indicator — color tier follows charge level. NaN = no reading yet.
  char bat_buf[16];
  Color bat_color = kDim;
  if (this->battery_sensor_ != nullptr &&
      !std::isnan(this->battery_sensor_->state)) {
    int pct = static_cast<int>(std::lround(this->battery_sensor_->state));
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    std::snprintf(bat_buf, sizeof(bat_buf), "BAT %d%%", pct);
    bat_color = battery_color_for_pct(pct);
  } else {
    std::snprintf(bat_buf, sizeof(bat_buf), "BAT --");
  }
  it.print(width / 2, 2, this->font_small_, bat_color,
           display::TextAlign::TOP_CENTER, bat_buf);

  const bool ha_known = (this->status_sensor_ != nullptr);
  const bool ha_ok = ha_known && this->status_sensor_->state;
  const char *ha_text = ha_known ? (ha_ok ? "HA: connected" : "HA: offline")
                                 : "HA: ?";
  Color ha_color = ha_known ? (ha_ok ? kGreen : kRed) : kDim;
  it.printf(width - 4, 2, this->font_small_, ha_color,
            display::TextAlign::TOP_RIGHT, "%s", ha_text);

  it.line(0, 18, width, 18, kDim);
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
      case Field::kInfo:
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
  switch (state) {
    case LearnState::kListening:
      headline = "Press a button on the OEM remote";
      headline_color = white;
      break;
    case LearnState::kCapturing:
      headline = "Capturing...";
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
    std::snprintf(line, sizeof(line), "valid packets: %u/%u",
                  cand.valid_packet_count,
                  ProFlame2Component::kLearnMinPackets);
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
