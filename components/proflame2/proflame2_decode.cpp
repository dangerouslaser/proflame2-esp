#include "proflame2_decode.h"

#include <cstring>

namespace esphome {
namespace proflame2 {

void ProFlame2Decoder::reset() {
  clear_buffer_();
  have_last_edge_ = false;
  last_edge_us_ = 0;
  level_at_last_edge_ = false;
  // Diagnostics survive reset() — they accumulate across captures.
}

void ProFlame2Decoder::clear_buffer_() {
  bit_count_ = 0;
  scan_start_offset_ = 0;
  std::memset(bit_buffer_, 0, sizeof(bit_buffer_));
}

bool ProFlame2Decoder::get_bit_(size_t index) const {
  return (bit_buffer_[index >> 3] >> (7 - (index & 7))) & 1u;
}

void ProFlame2Decoder::set_bit_(size_t index, bool value) {
  const uint8_t mask = static_cast<uint8_t>(1u << (7 - (index & 7)));
  if (value) {
    bit_buffer_[index >> 3] |= mask;
  } else {
    bit_buffer_[index >> 3] &= static_cast<uint8_t>(~mask);
  }
}

void ProFlame2Decoder::append_chips_(bool level, uint32_t count) {
  for (uint32_t i = 0; i < count; i++) {
    if (bit_count_ >= kBitBufferBits) {
      buffer_overflows_++;
      return;
    }
    set_bit_(bit_count_++, level);
    chips_ingested_++;
  }
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

// Try to decode a 7-word packet starting at chip offset `start` in the bit
// buffer. Returns true on success (last_packet_ populated, packets_emitted_
// incremented); false on any framing/validation failure.
bool ProFlame2Decoder::try_decode_packet_at_(size_t start) {
  if (start + kChipsPerPacket > bit_count_) {
    return false;
  }

  uint8_t word_data[kWordCount];
  bool word_pad[kWordCount];

  size_t pos = start;
  for (size_t w = 0; w < kWordCount; w++) {
    // 4-chip sync = 1110 (S marker "11" + start_guard data bit "10").
    if (!get_bit_(pos + 0) || !get_bit_(pos + 1) ||
        !get_bit_(pos + 2) || get_bit_(pos + 3)) {
      return false;
    }
    pos += kSyncChips;

    // 11 Manchester pairs → 11 data bits. ProFlame convention (matches
    // our TX path): chips "01" → bit 0, chips "10" → bit 1. Anything
    // else (00 or 11 mid-word) is a Manchester violation.
    uint16_t bits11 = 0;
    for (size_t b = 0; b < kDataBitsPerWord; b++) {
      const bool a = get_bit_(pos);
      const bool c = get_bit_(pos + 1);
      pos += 2;
      if (!a && c) {
        bits11 = static_cast<uint16_t>(bits11 << 1);  // bit 0
      } else if (a && !c) {
        bits11 = static_cast<uint16_t>((bits11 << 1) | 1u);  // bit 1
      } else {
        return false;
      }
    }

    // bits11 layout (MSB first): data[7:0] | pad | parity | end_guard.
    const uint8_t data = static_cast<uint8_t>((bits11 >> 3) & 0xFFu);
    const bool pad = (bits11 >> 2) & 1u;
    const bool parity = (bits11 >> 1) & 1u;
    const bool end_guard = bits11 & 1u;

    if (!end_guard) {
      return false;
    }
    const uint8_t expected_parity =
        parity_9(static_cast<uint16_t>(data) | (pad ? 0x100u : 0u));
    if (parity != expected_parity) {
      return false;
    }

    word_data[w] = data;
    word_pad[w] = pad;
  }

  // Pad-bit pattern: first word's pad must be 1, all others 0.
  if (!word_pad[0]) {
    return false;
  }
  for (size_t i = 1; i < kWordCount; i++) {
    if (word_pad[i]) {
      return false;
    }
  }

  // Field extraction (mirror of build_packet() in proflame2_cc1101.cpp).
  uint8_t c1, d1, c2, d2;
  invert_checksum(word_data[3], word_data[5], c1, d1);
  invert_checksum(word_data[4], word_data[6], c2, d2);

  last_packet_.serial = (static_cast<uint32_t>(word_data[0]) << 16) |
                        (static_cast<uint32_t>(word_data[1]) << 8) |
                        static_cast<uint32_t>(word_data[2]);
  last_packet_.cmd1 = word_data[3];
  last_packet_.cmd2 = word_data[4];
  last_packet_.chk1 = word_data[5];
  last_packet_.chk2 = word_data[6];
  last_packet_.c1 = c1;
  last_packet_.d1 = d1;
  last_packet_.c2 = c2;
  last_packet_.d2 = d2;

  packets_emitted_++;
  return true;
}

// Walk the bit buffer looking for a valid 7-word packet at any starting
// offset >= scan_start_offset_. Returns kPacketReady on the first match
// (and advances scan_start_offset_ past it); kPending if none found.
ProFlame2Decoder::Status ProFlame2Decoder::scan_framer_() {
  if (bit_count_ < kChipsPerPacket ||
      scan_start_offset_ + kChipsPerPacket > bit_count_) {
    return Status::kPending;
  }
  const size_t end = bit_count_ - kChipsPerPacket;
  for (size_t start = scan_start_offset_; start <= end; start++) {
    if (try_decode_packet_at_(start)) {
      scan_start_offset_ = start + kChipsPerPacket;
      return Status::kPacketReady;
    }
  }
  return Status::kPending;
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
  const bool prior_level = level_at_last_edge_;
  last_edge_us_ = timestamp_us;
  level_at_last_edge_ = level_after;

  // The line was at `prior_level` for `dt` microseconds. Round-to-nearest
  // chip count gives us a chip-rate-locked sample stream: a single jittered
  // edge no longer permanently shifts phase the way edge-time classification
  // would. Cap chips at a sensible upper bound so a multi-second silence
  // doesn't try to pack 10000 chips into the buffer.
  if (dt < kHalfBitUs / 2) {
    // Sub-half-chip blip — almost certainly a noise glitch. Skip.
    return Status::kPending;
  }
  uint32_t chips = (dt + kHalfBitUs / 2) / kHalfBitUs;
  if (chips > 16) {
    chips = 16;
  }
  append_chips_(prior_level, chips);

  // End-of-burst: long silence. Run the framer over what we have.
  // (We test prior_level here because a `1` chip of 6 ms would be radio
  //  signal, not silence — but ProFlame's longest legitimate run is 3
  //  chips, so any dt>6 ms IS end-of-burst regardless of level.)
  Status status = Status::kPending;
  if (dt > kBurstEndGapUs) {
    status = scan_framer_();
    if (status == Status::kPacketReady) {
      bursts_decoded_++;
    } else if (bit_count_ >= kChipsPerPacket) {
      bursts_failed_++;
    }
    clear_buffer_();
    return status;
  }

  // Buffer-full safety: shouldn't happen during a single normal burst, but
  // if the radio is chattering with noise we'll still try to decode rather
  // than silently lose data.
  if (bit_count_ >= kBitBufferBits - 32) {
    status = scan_framer_();
    if (status == Status::kPacketReady) {
      bursts_decoded_++;
    } else {
      bursts_failed_++;
    }
    clear_buffer_();
    return status;
  }

  // Opportunistic mid-burst decode: as soon as we have ≥1 packet's worth
  // of chips, try framing. This lets us emit packets without waiting for
  // the 6 ms tail-silence — useful for back-to-back repeats in the same
  // burst, and required by host tests that don't simulate trailing silence.
  if (bit_count_ >= kChipsPerPacket) {
    status = scan_framer_();
  }
  return status;
}

ProFlame2Decoder::Status ProFlame2Decoder::ingest_chip(bool chip) {
  if (bit_count_ >= kBitBufferBits) {
    buffer_overflows_++;
    return Status::kPending;
  }
  set_bit_(bit_count_++, chip);
  chips_ingested_++;

  // Re-scan whenever we've added enough chips for at least one full packet
  // and we land on a word boundary. This keeps host tests responsive
  // (callers feed chips in a loop and check the return value) without
  // re-scanning O(N) the whole buffer on every chip.
  if (bit_count_ >= kChipsPerPacket && (bit_count_ % kChipsPerWord) == 0) {
    return scan_framer_();
  }
  return Status::kPending;
}

}  // namespace proflame2
}  // namespace esphome
