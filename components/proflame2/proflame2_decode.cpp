#include "proflame2_decode.h"

namespace esphome {
namespace proflame2 {

void ProFlame2Decoder::reset() {
  bit_count_in_word_ = 0;
  current_word_data_ = 0;
  word_count_ = 0;
  for (size_t i = 0; i < kWordCount; i++) {
    word_data_[i] = 0;
    word_pad_[i] = false;
  }
  aligned_ = false;
  pending_chip_ = false;
  half_chip_pending_ = false;
  have_last_edge_ = false;
  last_edge_us_ = 0;
  level_at_last_edge_ = false;
  last_word_committed_us_ = 0;
  // Diagnostics survive reset() — they accumulate across captures.
}

uint8_t ProFlame2Decoder::parity_9(uint16_t data_with_pad) {
  uint8_t ones = 0;
  for (int i = 0; i < 9; i++) {
    if (data_with_pad & (1u << i)) {
      ones++;
    }
  }
  return ones & 1;
}

uint8_t ProFlame2Decoder::compute_checksum(uint8_t cmd, uint8_t c, uint8_t d) {
  // Mirror of ProFlame2Component::calculate_checksum() in proflame2_cc1101.cpp.
  // Kept here as standalone so the host-side decode tests don't pull in the
  // hardware component.
  const uint8_t hi = (cmd >> 4) & 0x0F;
  const uint8_t lo = cmd & 0x0F;
  const uint8_t x = (c ^ ((hi << 1) & 0x0F) ^ hi ^ ((lo << 1) & 0x0F)) & 0x0F;
  const uint8_t y = (d ^ hi ^ lo) & 0x0F;
  return static_cast<uint8_t>((x << 4) | y);
}

void ProFlame2Decoder::invert_checksum(uint8_t cmd, uint8_t chk, uint8_t &c,
                                       uint8_t &d) {
  // Direct algebra inverse — no search needed because c, d, and the partial
  // sums all collapse into 4-bit nibbles after the &0x0F masks.
  const uint8_t hi = (cmd >> 4) & 0x0F;
  const uint8_t lo = cmd & 0x0F;
  const uint8_t x = (chk >> 4) & 0x0F;
  const uint8_t y = chk & 0x0F;
  d = static_cast<uint8_t>((y ^ hi ^ lo) & 0x0F);
  c = static_cast<uint8_t>((x ^ ((hi << 1) & 0x0F) ^ hi ^ ((lo << 1) & 0x0F)) & 0x0F);
}

void ProFlame2Decoder::framing_reset_() {
  framing_resets_++;
  bit_count_in_word_ = 0;
  current_word_data_ = 0;
  word_count_ = 0;
  aligned_ = false;
  pending_chip_ = false;
  // Keep the edge timing state so the next ingest_edge() can still classify
  // pulse widths against the most recent edge — that's how we recover from
  // a bit error mid-packet without losing bit-time sync.
}

ProFlame2Decoder::Status ProFlame2Decoder::ingest_chip(bool chip) {
  if (!aligned_) {
    // Sliding 2-chip window looking for the "11" S marker.
    if (!pending_chip_) {
      pending_chip_ = true;
      half_chip_pending_ = chip;  // store
      return Status::kPending;
    }
    const bool a = half_chip_pending_;
    const bool b = chip;
    if (a && b) {
      // Found S marker — align here.
      aligned_ = true;
      pending_chip_ = false;
      bit_count_in_word_ = 0;
      current_word_data_ = 0;
      return Status::kPending;
    }
    // Not aligned; advance window by 1.
    half_chip_pending_ = chip;
    return Status::kPending;
  }

  // Aligned — pair up chips into bits.
  if (!pending_chip_) {
    pending_chip_ = true;
    half_chip_pending_ = chip;
    return Status::kPending;
  }
  const bool a = half_chip_pending_;
  const bool b = chip;
  pending_chip_ = false;

  if (!a && b) {
    // "01" → bit 0
    current_word_data_ = static_cast<uint16_t>((current_word_data_ << 1) & 0x1FFF);
    bit_count_in_word_++;
  } else if (a && !b) {
    // "10" → bit 1
    current_word_data_ = static_cast<uint16_t>(((current_word_data_ << 1) | 1) & 0x1FFF);
    bit_count_in_word_++;
  } else if (a && b) {
    // "11" mid-stream — second S marker. Treat as start of a new word: drop
    // anything we'd accumulated, stay aligned. This is the resync path when
    // we miss bits but the next packet's S still arrives on a chip boundary.
    framing_reset_();
    aligned_ = true;
    bit_count_in_word_ = 0;
    current_word_data_ = 0;
    return Status::kPending;
  } else {
    // "00" — Manchester violation, full resync.
    framing_reset_();
    return Status::kPending;
  }

  if (bit_count_in_word_ == kWordDataBits) {
    return validate_and_commit_word_();
  }
  return Status::kPending;
}

ProFlame2Decoder::Status
ProFlame2Decoder::validate_and_commit_word_() {
  // current_word_data_ holds 12 bits, MSB first (bit 11 = start_guard, ...).
  const uint16_t w = current_word_data_;
  const bool start_guard = (w >> 11) & 1;
  const uint8_t data = static_cast<uint8_t>((w >> 3) & 0xFF);
  const bool pad = (w >> 2) & 1;
  const bool parity = (w >> 1) & 1;
  const bool end_guard = w & 1;

  if (!start_guard || !end_guard) {
    framing_reset_();
    return Status::kPending;
  }

  const uint8_t expected_parity =
      parity_9(static_cast<uint16_t>(data) | (pad ? 0x100 : 0));
  if (parity != expected_parity) {
    framing_reset_();
    return Status::kPending;
  }

  word_data_[word_count_] = data;
  word_pad_[word_count_] = pad;
  word_count_++;
  bit_count_in_word_ = 0;
  current_word_data_ = 0;

  if (word_count_ == kWordCount) {
    return finalize_packet_();
  }

  // Between words we need the next "11" S marker to re-anchor pair alignment.
  // Crucially, we keep word_count_ + word_data_ + word_pad_ — those carry the
  // packet across word boundaries. (A hard framing_reset_() would zero them.)
  aligned_ = false;
  pending_chip_ = false;
  return Status::kPending;
}

ProFlame2Decoder::Status ProFlame2Decoder::finalize_packet_() {
  // First word's pad bit must be 1 (it's the only one); rest must be 0.
  if (!word_pad_[0]) {
    framing_reset_();
    return Status::kPending;
  }
  for (size_t i = 1; i < kWordCount; i++) {
    if (word_pad_[i]) {
      framing_reset_();
      return Status::kPending;
    }
  }

  const uint8_t serial1 = word_data_[0];
  const uint8_t serial2 = word_data_[1];
  const uint8_t serial3 = word_data_[2];
  const uint8_t cmd1 = word_data_[3];
  const uint8_t cmd2 = word_data_[4];
  const uint8_t chk1 = word_data_[5];
  const uint8_t chk2 = word_data_[6];

  uint8_t c1, d1, c2, d2;
  invert_checksum(cmd1, chk1, c1, d1);
  invert_checksum(cmd2, chk2, c2, d2);

  // Cross-validate: the inferred (c, d) must reproduce the captured checksum
  // exactly. This is an algebraic identity for valid packets, but a noise-
  // corrupted packet that somehow survived parity will fail this check.
  if (compute_checksum(cmd1, c1, d1) != chk1 ||
      compute_checksum(cmd2, c2, d2) != chk2) {
    framing_reset_();
    return Status::kPending;
  }

  last_packet_.serial =
      (static_cast<uint32_t>(serial1) << 16) |
      (static_cast<uint32_t>(serial2) << 8) | static_cast<uint32_t>(serial3);
  last_packet_.cmd1 = cmd1;
  last_packet_.cmd2 = cmd2;
  last_packet_.chk1 = chk1;
  last_packet_.chk2 = chk2;
  last_packet_.c1 = c1;
  last_packet_.d1 = d1;
  last_packet_.c2 = c2;
  last_packet_.d2 = d2;

  // Reset framer state but preserve last_packet_ — caller reads it via
  // get_packet(). aligned_ goes false so we wait for the next S marker
  // before parsing a new packet (e.g. a repeat in the same burst).
  word_count_ = 0;
  bit_count_in_word_ = 0;
  current_word_data_ = 0;
  aligned_ = false;
  pending_chip_ = false;

  return Status::kPacketReady;
}

ProFlame2Decoder::Status
ProFlame2Decoder::ingest_edge(uint32_t timestamp_us, bool level_after) {
  total_edges_++;

  if (!have_last_edge_) {
    have_last_edge_ = true;
    last_edge_us_ = timestamp_us;
    level_at_last_edge_ = level_after;
    return Status::kPending;
  }

  const uint32_t dt = timestamp_us - last_edge_us_;
  const bool level_held = level_at_last_edge_;
  last_edge_us_ = timestamp_us;
  level_at_last_edge_ = level_after;

  // Inter-word gap clear sign of repeat boundary. Drop any in-progress
  // alignment so the next S marker reanchors us.
  if (dt > kMaxInterWordGapUs) {
    framing_reset_();
    return Status::kPending;
  }

  uint32_t chip_count = 0;
  if (dt >= kHalfBitMinUs && dt <= kHalfBitMaxUs) {
    chip_count = 1;
  } else if (dt >= kBitMinUs && dt <= kBitMaxUs) {
    chip_count = 2;
  } else if (dt >= kThreeChipMinUs && dt <= kThreeChipMaxUs) {
    chip_count = 3;
  } else {
    framing_reset_();
    return Status::kPending;
  }

  for (uint32_t i = 0; i < chip_count; i++) {
    const Status s = ingest_chip(level_held);
    if (s == Status::kPacketReady) {
      return s;
    }
  }
  return Status::kPending;
}

}  // namespace proflame2
}  // namespace esphome
