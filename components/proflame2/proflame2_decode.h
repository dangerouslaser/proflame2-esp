#pragma once

// Pure-logic decoder for ProFlame 2 RF packets. Zero ESPHome dependencies —
// only standard library — so it can be unit-tested on the host without a
// flash/serial cycle. The device build links it through ESPHome's normal
// component pipeline; the host tests link it through components/proflame2/test/.
//
// Pipeline (all stages live in this one translation unit):
//   edge stream
//     -> pulse-width classifier (edge dt -> chip count of held level)
//     -> Manchester pair decoder (01->0, 10->1, 11->S, 00->resync)
//     -> 13-bit word framer (validates guard bits + parity)
//     -> 91-bit packet aggregator (7 words; first word pad=1, rest pad=0)
//     -> field extraction (serial[3], cmd1, cmd2, chk1, chk2)
//     -> ECC inversion (direct algebra, no search) recovering c1,d1,c2,d2
//
// State is reset on any framing failure; the next valid S marker re-aligns.
// All numeric tolerances are deliberately wide (±40% of bit time) to survive
// WiFi-induced ISR jitter on ESP32-S3.

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace proflame2 {

struct DecodedPacket {
  uint32_t serial;   // 24-bit, low bits
  uint8_t cmd1;
  uint8_t cmd2;
  uint8_t chk1;
  uint8_t chk2;
  // ECC pairing constants inferred from (cmd, chk) pairs. Only low 4 bits
  // are protocol-meaningful; high 4 bits zeroed.
  uint8_t c1;
  uint8_t d1;
  uint8_t c2;
  uint8_t d2;
};

class ProFlame2Decoder {
 public:
  // ProFlame 2 timing constants.
  static constexpr uint32_t kHalfBitUs = 208;     // 4800 chip rate
  static constexpr uint32_t kBitUs = 417;         // 2400 baud
  static constexpr uint32_t kMaxInterWordGapUs = 5000;

  // Pulse-width tolerance: ±40% of half-bit time. Wide enough to survive
  // WiFi-driven ISR jitter without falsely rejecting valid packets.
  static constexpr uint32_t kHalfBitMinUs = kHalfBitUs * 60 / 100;     // 124
  static constexpr uint32_t kHalfBitMaxUs = kHalfBitUs * 140 / 100;    // 291
  static constexpr uint32_t kBitMinUs = kBitUs * 60 / 100;             // 250
  static constexpr uint32_t kBitMaxUs = kBitUs * 140 / 100;            // 583
  static constexpr uint32_t kThreeChipMinUs = kHalfBitUs * 3 * 60 / 100;  // 374
  static constexpr uint32_t kThreeChipMaxUs = kHalfBitUs * 3 * 140 / 100; // 873

  // Packet dimensions.
  static constexpr size_t kWordCount = 7;
  static constexpr size_t kWordBits = 13;          // S + 12 data bits
  static constexpr size_t kWordDataBits = 12;      // bits AFTER the S marker

  enum class Status : uint8_t {
    kPending,        // need more edges/chips
    kPacketReady,    // get_packet() reflects a fresh, validated packet
  };

  void reset();

  // Feed an edge from the radio: timestamp_us is when the GDO0 transition
  // happened; level_after is the GDO0 level the chip has after the edge.
  // Internally classifies pulse width and pushes 1, 2, or 3 chips.
  Status ingest_edge(uint32_t timestamp_us, bool level_after);

  // Feed a single chip directly. Useful for tests that bypass timing.
  Status ingest_chip(bool chip);

  const DecodedPacket &get_packet() const { return last_packet_; }

  // Diagnostics — not consumed by callers, just for log/test introspection.
  uint32_t total_edges() const { return total_edges_; }
  uint32_t framing_resets() const { return framing_resets_; }

  // Helpers exposed for tests + ECC inversion reuse.
  static uint8_t parity_9(uint16_t data_with_pad);
  static uint8_t compute_checksum(uint8_t cmd, uint8_t c, uint8_t d);
  static void invert_checksum(uint8_t cmd, uint8_t chk, uint8_t &c, uint8_t &d);

 private:
  // Word framing state.
  uint8_t bit_count_in_word_{0};   // 0..kWordDataBits
  uint16_t current_word_data_{0};  // accumulated 12 bits (excl. S marker)

  // Packet state.
  uint8_t word_count_{0};          // number of validated words committed
  uint8_t word_data_[kWordCount]{};
  bool word_pad_[kWordCount]{};

  // Manchester pair tracking.
  bool aligned_{false};
  bool half_chip_pending_{false};
  bool pending_chip_{false};       // the lone chip not yet paired

  // Edge-to-chip state.
  bool have_last_edge_{false};
  uint32_t last_edge_us_{0};
  bool level_at_last_edge_{false};
  uint32_t last_word_committed_us_{0};

  // Decoded result.
  DecodedPacket last_packet_{};

  // Diagnostics.
  uint32_t total_edges_{0};
  uint32_t framing_resets_{0};

  // Internal helpers (in proflame2_decode.cpp).
  void framing_reset_();
  Status validate_and_commit_word_();
  Status finalize_packet_();
};

}  // namespace proflame2
}  // namespace esphome
