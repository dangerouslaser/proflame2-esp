#include "proflame2_ui.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

#include <algorithm>
#include <cstdio>

namespace esphome {
namespace proflame2 {

static const char *const TAG = "proflame2.ui";

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
      }
      this->last_button_state_ = pressed;
    });
  } else {
    ESP_LOGW(TAG, "No encoder button bound — selection input disabled");
  }

  this->last_interaction_ms_ = millis();
}

void ProFlame2UI::loop() {
  // Reserved for future timeout-driven behavior (backlight dim after 30 s,
  // menu auto-cancel, etc.). Intentionally empty in this commit.
}

void ProFlame2UI::on_encoder_delta_(int direction) {
  this->last_interaction_ms_ = millis();
  this->apply_delta_to_selected_(direction);
}

void ProFlame2UI::on_button_press_() {
  this->last_interaction_ms_ = millis();
  this->cycle_selection_();
}

void ProFlame2UI::cycle_selection_() {
  uint8_t next = static_cast<uint8_t>(this->selected_) + 1;
  if (next >= static_cast<uint8_t>(Field::kCount)) {
    next = 0;
  }
  this->selected_ = static_cast<Field>(next);
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
    case Field::kFlame: return "FLAME";
    case Field::kFan:   return "FAN";
    case Field::kPower: return "POWER";
    case Field::kLight: return "LIGHT";
    default:            return "?";
  }
}

int ProFlame2UI::field_value_(Field f) const {
  if (this->parent_ == nullptr) {
    return 0;
  }
  const auto &state = this->parent_->current_state_;
  switch (f) {
    case Field::kFlame: return state.flame_level;
    case Field::kFan:   return state.fan_level;
    case Field::kPower: return state.power ? 1 : 0;
    case Field::kLight: return state.light_level;
    default:            return 0;
  }
}

void ProFlame2UI::draw(display::Display &it) {
  if (this->parent_ == nullptr) {
    return;
  }

  const int width = it.get_width();
  const int height = it.get_height();
  const auto white = Color(0xFF, 0xFF, 0xFF);
  const auto dim = Color(0x80, 0x80, 0x80);

  // Status bar
  if (this->font_small_ != nullptr) {
    it.print(4, 2, this->font_small_, white, "ProFlame");
    const bool ha_known = (this->status_sensor_ != nullptr);
    const bool ha_ok = ha_known && this->status_sensor_->state;
    const char *ha_text = ha_known ? (ha_ok ? "HA: connected" : "HA: offline")
                                   : "HA: ?";
    it.printf(width - 4, 2, this->font_small_, white,
              display::TextAlign::TOP_RIGHT, "%s", ha_text);
  }

  // Divider
  it.line(0, 18, width, 18, white);

  // Field rows. Selected row uses the larger value font; non-selected stays
  // medium. Leaves last ~12 px as a footer reservation for future status hints.
  const int row_h = (height - 24 - 12) / static_cast<int>(Field::kCount);
  int y = 24;
  for (int i = 0; i < static_cast<int>(Field::kCount); i++) {
    const Field f = static_cast<Field>(i);
    const bool selected = (f == this->selected_);
    const Color label_color = selected ? white : dim;

    if (selected && this->font_medium_ != nullptr) {
      it.print(4, y + 4, this->font_medium_, white, ">");
    }

    if (this->font_medium_ != nullptr) {
      it.print(28, y + 4, this->font_medium_, label_color,
               this->field_label_(f));
    }

    char buf[16];
    if (f == Field::kPower) {
      snprintf(buf, sizeof(buf), "%s",
               this->field_value_(f) ? "ON" : "OFF");
    } else {
      snprintf(buf, sizeof(buf), "%d", this->field_value_(f));
    }

    font::Font *value_font = (selected && this->font_large_ != nullptr)
                                 ? this->font_large_
                                 : this->font_medium_;
    if (value_font != nullptr) {
      it.printf(width - 8, y + 2, value_font, white,
                display::TextAlign::TOP_RIGHT, "%s", buf);
    }

    y += row_h;
  }
}

}  // namespace proflame2
}  // namespace esphome
