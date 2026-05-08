#include "proflame2_cc1101.h"
#include "esphome/core/application.h"
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
  // Same fix learn_confirm() applies: the RX register table leaves
  // FOCCFG / AGCCTRL2/1/0 at envelope-detector values that the TX table
  // doesn't reset. Without a full reinit, the next TX after a cancel
  // emits modulation the IFC can't decode (MARCSTATE looks fine, but the
  // fireplace doesn't react). Symmetric with learn_confirm — every path
  // out of learn-mode needs to scrub the radio.
  this->reinit_radio_();
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

  // Bring the CC1101 back to the same clean register set setup() leaves it
  // in. Without this, the next TX inherits the RX-mode FOCCFG / AGCCTRL2/1/0
  // values from learn-mode (the TX table doesn't write those four regs), and
  // even though MARCSTATE looks correct the chip transmits modulation the
  // fireplace can't decode — the symptom that previously required a reboot
  // to clear.
  this->reinit_radio_();

  this->learn_state_ = LearnState::kPersisted;
  this->learn_persisted_ms_ = millis();
  this->publish_diagnostic_sensors_();
  ESP_LOGI(TAG_LEARN,
           "Learned values committed: serial=0x%06X c1=0x%X d1=0x%X "
           "c2=0x%X d2=0x%X",
           this->serial_number_, this->ecc_c1_, this->ecc_d1_, this->ecc_c2_,
           this->ecc_d2_);
  return true;
}

namespace {
void seed_candidate_(ProFlame2Component::LearnCandidate &c,
                     const DecodedPacket &p) {
  c = ProFlame2Component::LearnCandidate{};
  c.serial = p.serial;
  c.c1 = p.c1; c.d1 = p.d1; c.c2 = p.c2; c.d2 = p.d2;
  c.cmd1.seed(p.cmd1);
  c.cmd2.seed(p.cmd2);
  c.valid_packet_count = 1;
}
}  // namespace

void ProFlame2Component::on_packet_decoded_(const DecodedPacket &p) {
  // Only progress when we're actively learning. Stale packets that arrive
  // after a cancel get dropped.
  if (this->learn_state_ != LearnState::kListening &&
      this->learn_state_ != LearnState::kCapturing) {
    return;
  }

  auto &c = this->learn_candidate_;

  if (c.valid_packet_count == 0) {
    seed_candidate_(c, p);
    this->learn_state_ = LearnState::kCapturing;
    ESP_LOGI(TAG_LEARN,
             "First valid packet: serial=0x%06X cmd1=0x%02X cmd2=0x%02X "
             "→ c1=0x%X d1=0x%X c2=0x%X d2=0x%X "
             "(press a different button to validate ECC)",
             p.serial, p.cmd1, p.cmd2, p.c1, p.d1, p.c2, p.d2);
    return;
  }

  if (p.serial != c.serial) {
    // Different remote (or framing artifact picking up a different burst).
    // Reseed from the new packet — same authoritative-restart behavior we've
    // always had. Doesn't indicate a formula problem, just that a second
    // remote is in range or a stray decode landed.
    ESP_LOGW(TAG_LEARN,
             "Serial mismatch — restarting capture (was 0x%06X got 0x%06X)",
             c.serial, p.serial);
    seed_candidate_(c, p);
    return;
  }

  const bool ecc_agrees = (p.c1 == c.c1) && (p.d1 == c.d1) &&
                          (p.c2 == c.c2) && (p.d2 == c.d2);
  if (!ecc_agrees) {
    if (c.cmd1.contains(p.cmd1) && c.cmd2.contains(p.cmd2)) {
      // (cmd1, cmd2) we've already inverted, but a different (c, d) came
      // out — algebraically impossible from the formula, so this packet is
      // a corrupted decode (parity-passed bit shift, etc.). Drop without
      // disturbing the candidate or the diversity counts.
      ESP_LOGW(TAG_LEARN,
               "ECC drift on known cmd_bytes (cmd1=0x%02X cmd2=0x%02X) — "
               "treating as decoder noise, ignoring this packet",
               p.cmd1, p.cmd2);
      return;
    }
    // Different cmd_byte that inverts to a different (c, d) is the smoking
    // gun: the inversion formula does not fit this remote (likely a
    // ProFlame variant with different word order or XOR pattern). Bail to
    // kFailed with a diagnostic instead of pseudo-converging on garbage.
    ESP_LOGE(TAG_LEARN,
             "ECC formula mismatch across distinct cmd_bytes — your remote "
             "may use a non-standard ProFlame variant. "
             "Press 1: cmd1=0x%02X cmd2=0x%02X → c1=0x%X d1=0x%X c2=0x%X d2=0x%X. "
             "Press 2: cmd1=0x%02X cmd2=0x%02X → c1=0x%X d1=0x%X c2=0x%X d2=0x%X. "
             "See docs/troubleshooting.md.",
             c.cmd1.seen[0], c.cmd2.seen[0], c.c1, c.d1, c.c2, c.d2,
             p.cmd1, p.cmd2, p.c1, p.d1, p.c2, p.d2);
    this->stop_rx_capture();
    this->learn_state_ = LearnState::kFailed;
    return;
  }

  c.cmd1.record(p.cmd1);
  c.cmd2.record(p.cmd2);
  c.valid_packet_count++;

  ESP_LOGI(TAG_LEARN,
           "%u packets agree (distinct cmd1=%u/%u, cmd2=%u/%u)",
           c.valid_packet_count, c.cmd1.distinct, kLearnMinDistinctCmds,
           c.cmd2.distinct, kLearnMinDistinctCmds);

  if (c.valid_packet_count >= kLearnMinPackets &&
      c.cmd1.distinct >= kLearnMinDistinctCmds &&
      c.cmd2.distinct >= kLearnMinDistinctCmds) {
    this->learn_state_ = LearnState::kConverged;
    ESP_LOGI(TAG_LEARN,
             "CONVERGED — awaiting user confirm: serial=0x%06X c1=0x%X "
             "d1=0x%X c2=0x%X d2=0x%X (validated across %u distinct cmd1, "
             "%u distinct cmd2)",
             c.serial, c.c1, c.d1, c.c2, c.d2, c.cmd1.distinct,
             c.cmd2.distinct);
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
               "Timed out after %u ms with %u valid packets "
               "(distinct cmd1=%u, cmd2=%u — need ≥%u of each)",
               static_cast<unsigned>(kLearnTimeoutMs),
               this->learn_candidate_.valid_packet_count,
               this->learn_candidate_.cmd1.distinct,
               this->learn_candidate_.cmd2.distinct, kLearnMinDistinctCmds);
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

void ProFlame2Component::clear_learned_state() {
  // Stamp an explicitly-invalid blob over the existing one. load_learned_state_
  // checks (version, kFlagValid, CRC) — we fail the flags check immediately,
  // so a fresh boot falls back to YAML defaults.
  ProFlame2LearnedState blob{};
  blob.version = ProFlame2LearnedState::kCurrentVersion;
  blob.flags = 0;  // ~kFlagValid
  this->pref_learned_.save(&blob);
  global_preferences->sync();
  ESP_LOGI(TAG_LEARN, "Pairing cleared from NVS — rebooting to apply YAML defaults");
  // Defer the reboot so the log line gets flushed and any in-flight TX repeats
  // finish gracefully. safe_reboot() runs the proper shutdown path (callbacks,
  // OTA-pending handling, etc.) instead of a hard reset.
  App.safe_reboot();
}

}  // namespace proflame2
}  // namespace esphome
