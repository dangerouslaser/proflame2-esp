#include "proflame2_battery.h"
#include "esphome/core/log.h"

namespace esphome {
namespace proflame2 {

static const char *const TAG = "proflame2.battery";

// BQ27220 standard-command register for the cached state-of-charge percent.
// Source: examples/bq27xxx_test/bq27220_def.h in the LilyGo T-Embed CC1101
// repo. The chip returns a 16-bit little-endian value clamped 0..100.
static constexpr uint8_t kCommandStateOfCharge = 0x2C;

void ProFlame2Battery::update() {
  uint8_t data[2] = {0, 0};
  if (!this->read_bytes(kCommandStateOfCharge, data, 2)) {
    ESP_LOGW(TAG, "I2C read of StateOfCharge (0x%02X) failed", kCommandStateOfCharge);
    this->status_set_warning();
    return;
  }
  uint16_t soc = static_cast<uint16_t>(data[0]) |
                 (static_cast<uint16_t>(data[1]) << 8);
  // Sane upper bound — the chip should never return >100 in healthy state,
  // but uninitialized fuel gauges have been seen returning 0xFFFF before
  // first calibration.
  if (soc > 100) {
    ESP_LOGD(TAG, "Out-of-range SoC 0x%04X — clamping to 100", soc);
    soc = 100;
  }
  this->publish_state(static_cast<float>(soc));
  this->status_clear_warning();
}

void ProFlame2Battery::dump_config() {
  ESP_LOGCONFIG(TAG, "ProFlame 2 Battery (BQ27220):");
  LOG_I2C_DEVICE(this);
  LOG_UPDATE_INTERVAL(this);
  LOG_SENSOR("  ", "Battery", this);
}

}  // namespace proflame2
}  // namespace esphome
