#include "proflame2_cc1101.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace proflame2 {

static const char *const TAG_LEARN = "proflame2.learn";

void ProFlame2Component::learn_start() {
  if (this->learn_state_ != LearnState::kIdle &&
      this->learn_state_ != LearnState::kFailed) {
    ESP_LOGW(TAG_LEARN, "learn_start ignored — already in state %u",
             static_cast<unsigned>(this->learn_state_));
    return;
  }

  this->learn_decoder_.reset();
  this->learn_candidate_ = LearnCandidate{};
  this->learn_started_ms_ = millis();
  this->learn_state_ = LearnState::kListening;

  this->start_rx_capture();
  if (!this->rx_active_) {
    // start_rx_capture refused (TX in flight, etc.). Bounce back to kIdle.
    ESP_LOGW(TAG_LEARN, "RX could not start; aborting learn");
    this->learn_state_ = LearnState::kFailed;
    return;
  }
  ESP_LOGI(TAG_LEARN, "Learn-mode armed — press any button on the OEM remote");
}

void ProFlame2Component::learn_cancel() {
  if (this->learn_state_ == LearnState::kIdle) {
    return;
  }
  this->stop_rx_capture();
  this->learn_state_ = LearnState::kIdle;
  this->learn_candidate_ = LearnCandidate{};
  ESP_LOGI(TAG_LEARN, "Learn-mode cancelled");
}

bool ProFlame2Component::learn_confirm() {
  if (this->learn_state_ != LearnState::kConverged) {
    ESP_LOGW(TAG_LEARN, "learn_confirm called outside kConverged (state=%u)",
             static_cast<unsigned>(this->learn_state_));
    return false;
  }

  const auto &c = this->learn_candidate_;
  const bool ok = this->save_learned_state_(c.serial, c.c1, c.d1, c.c2, c.d2);
  this->stop_rx_capture();

  if (!ok) {
    ESP_LOGE(TAG_LEARN, "NVS save failed; staying converged for retry");
    return false;
  }

  // Apply learned values to the live members so subsequent transmits use them
  // immediately (no reboot required).
  this->serial_number_ = c.serial & 0x00FFFFFFu;
  this->ecc_c1_ = c.c1 & 0x0F;
  this->ecc_d1_ = c.d1 & 0x0F;
  this->ecc_c2_ = c.c2 & 0x0F;
  this->ecc_d2_ = c.d2 & 0x0F;
  this->config_source_ = ConfigSource::kNvsLearned;

  this->learn_state_ = LearnState::kPersisted;
  this->learn_persisted_ms_ = millis();
  ESP_LOGI(TAG_LEARN,
           "Learned values committed: serial=0x%06X c1=0x%X d1=0x%X "
           "c2=0x%X d2=0x%X",
           this->serial_number_, this->ecc_c1_, this->ecc_d1_, this->ecc_c2_,
           this->ecc_d2_);
  return true;
}

void ProFlame2Component::on_packet_decoded_(const DecodedPacket &p) {
  // Only progress when we're actively learning. Stale packets that arrive
  // after a cancel get dropped.
  if (this->learn_state_ != LearnState::kListening &&
      this->learn_state_ != LearnState::kCapturing) {
    return;
  }

  if (this->learn_candidate_.valid_packet_count == 0) {
    this->learn_candidate_.serial = p.serial;
    this->learn_candidate_.c1 = p.c1;
    this->learn_candidate_.d1 = p.d1;
    this->learn_candidate_.c2 = p.c2;
    this->learn_candidate_.d2 = p.d2;
    this->learn_candidate_.valid_packet_count = 1;
    this->learn_state_ = LearnState::kCapturing;
    ESP_LOGI(TAG_LEARN,
             "First valid packet: serial=0x%06X c1=0x%X d1=0x%X c2=0x%X "
             "d2=0x%X",
             p.serial, p.c1, p.d1, p.c2, p.d2);
    return;
  }

  // Subsequent packets must agree exactly. Mismatch → discard everything and
  // restart the capture from this packet (treat the new one as authoritative
  // for the next attempt). Safer than averaging or majority-vote: gas
  // appliance, we want every accepted packet to corroborate.
  const auto &c = this->learn_candidate_;
  const bool agrees = (p.serial == c.serial) && (p.c1 == c.c1) &&
                      (p.d1 == c.d1) && (p.c2 == c.c2) && (p.d2 == c.d2);
  if (!agrees) {
    ESP_LOGW(TAG_LEARN,
             "Packet mismatch — restarting capture (was serial=0x%06X "
             "got 0x%06X)",
             c.serial, p.serial);
    this->learn_candidate_ = LearnCandidate{};
    this->learn_candidate_.serial = p.serial;
    this->learn_candidate_.c1 = p.c1;
    this->learn_candidate_.d1 = p.d1;
    this->learn_candidate_.c2 = p.c2;
    this->learn_candidate_.d2 = p.d2;
    this->learn_candidate_.valid_packet_count = 1;
    return;
  }

  this->learn_candidate_.valid_packet_count++;
  ESP_LOGI(TAG_LEARN, "%u/%u valid packets agree",
           this->learn_candidate_.valid_packet_count, kLearnMinPackets);

  if (this->learn_candidate_.valid_packet_count >= kLearnMinPackets) {
    this->learn_state_ = LearnState::kConverged;
    ESP_LOGI(TAG_LEARN,
             "CONVERGED — awaiting user confirm: serial=0x%06X c1=0x%X "
             "d1=0x%X c2=0x%X d2=0x%X",
             c.serial, c.c1, c.d1, c.c2, c.d2);
  }
}

void ProFlame2Component::service_learn_() {
  if (this->learn_state_ == LearnState::kIdle) {
    return;
  }

  const uint32_t now = millis();

  if (this->learn_state_ == LearnState::kListening ||
      this->learn_state_ == LearnState::kCapturing) {
    if (now - this->learn_started_ms_ > kLearnTimeoutMs) {
      ESP_LOGW(TAG_LEARN,
               "Timed out after %u ms with %u valid packets",
               static_cast<unsigned>(kLearnTimeoutMs),
               this->learn_candidate_.valid_packet_count);
      this->stop_rx_capture();
      this->learn_state_ = LearnState::kFailed;
    }
    return;
  }

  if (this->learn_state_ == LearnState::kPersisted) {
    if (now - this->learn_persisted_ms_ > kLearnPersistedHoldMs) {
      this->learn_state_ = LearnState::kIdle;
    }
    return;
  }

  // kConverged or kFailed: wait for user input (confirm/cancel via UI).
}

bool ProFlame2Component::save_learned_state_(uint32_t serial, uint8_t c1,
                                             uint8_t d1, uint8_t c2,
                                             uint8_t d2) {
  // pref_learned_ is initialized by load_learned_state_() in setup(); by the
  // time the user reaches a learn_confirm we're guaranteed it's wired up.
  ProFlame2LearnedState blob{};
  blob.version = ProFlame2LearnedState::kCurrentVersion;
  blob.flags = ProFlame2LearnedState::kFlagValid;
  blob.serial = serial & 0x00FFFFFFu;
  blob.c1 = c1 & 0x0F;
  blob.d1 = d1 & 0x0F;
  blob.c2 = c2 & 0x0F;
  blob.d2 = d2 & 0x0F;
  // reserved[6] zero-init from {} above.

  // CRC over everything except the trailing crc32 field. Same routine that
  // load_learned_state_() uses for verification.
  const size_t crc_len = sizeof(ProFlame2LearnedState) - sizeof(uint32_t);
  blob.crc32 =
      ProFlame2Component::crc32_iso_(reinterpret_cast<const uint8_t *>(&blob),
                                     crc_len);

  if (!this->pref_learned_.save(&blob)) {
    return false;
  }
  // ESPHome buffers prefs in RAM until next flush; force commit so a power
  // loss right after confirm doesn't lose the pairing.
  global_preferences->sync();
  return true;
}

}  // namespace proflame2
}  // namespace esphome
