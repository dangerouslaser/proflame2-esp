#pragma once

// Pure-logic decoder for ProFlame 2 RF packets. Zero ESPHome dependencies —
// only standard library — so it can be unit-tested on the host without a
// flash/serial cycle. The device build links it through ESPHome's normal
// component pipeline; the host tests link it through components/proflame2/test/.
//
// Architecture: rtl_433-style buffered + PCM, NOT streaming.
//
//   edge stream
//     -> per-edge: round dt to nearest chip count, append N held-level chips
//        to a flat bit buffer (chip-rate-locked sampling — phase cannot drift
//        from a single jittered edge)
//     -> on burst end (gap > 6 ms) or buffer-full: scan_framer_() walks the
//        buffer offline, looking at every offset for the 4-chip "1110" sync
//        followed by 11 Manchester-decoded data bits, repeated 7 times
//     -> on a valid 7-word match: extract fields, run ECC inversion (direct
//        algebra), publish DecodedPacket
//
// Why buffered: a streaming Manchester decoder is fragile to ISR jitter —
// any one ambiguous edge classification permanently shifts chip phase, and
// no amount of soft-resetting can reconstruct the lost alignment without
// re-scanning. The PCM-style sampler used here can't lose phase the same
// way because each chip is anchored to chip-rate time, not edge time.
//
// Reference: github.com/merbanan/rtl_433 src/devices/proflame2.c, and
// github.com/NorthernMan54/rtl_433_ESP for the CC1101 register strategy.

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
  // ProFlame 2 chip rate: 1/2400 baud ≈ 416 µs per Manchester chip. A data
  // bit (= 2 chips) is therefore ~833 µs.
  static constexpr uint32_t kHalfBitUs = 417;
  static constexpr uint32_t kBitUs = 833;

  // Inter-burst silence threshold. Below this, an edge belongs to the
  // current burst; above it, the burst is complete and we can run the
  // framer. Mirrors rtl_433's reset_limit=6000 in proflame2.c.
  static constexpr uint32_t kBurstEndGapUs = 6000;

  // Packet dimensions.
  //   sync per word = 4 chips (1110 = S + start_guard chip pair)
  //   data per word = 11 Manchester bits (= 22 chips) [8 data + pad + parity + end_guard]
  //   total per word = 26 chips
  //   words per packet = 7
  //   chips per packet = 182
  static constexpr size_t kWordCount = 7;
  static constexpr size_t kSyncChips = 4;
  static constexpr size_t kDataBitsPerWord = 11;
  static constexpr size_t kChipsPerWord = kSyncChips + 2 * kDataBitsPerWord;
  static constexpr size_t kChipsPerPacket = kWordCount * kChipsPerWord;

  // Bit-buffer size. Sized for one full burst (5 packet repeats × 182 chips
  // = 910 chips) plus headroom for inter-packet noise and chip-time
  // rounding. Powers of 2 keep the bit-set/get arithmetic cheap.
  static constexpr size_t kBitBufferBytes = 512;
  static constexpr size_t kBitBufferBits = kBitBufferBytes * 8;

  enum class Status : uint8_t {
    kPending,        // need more edges/chips
    kPacketReady,    // get_packet() reflects a fresh, validated packet
  };

  void reset();

  // Feed an edge from the radio: timestamp_us is when the GDO0 transition
  // happened; level_after is the GDO0 level the chip has after the edge.
  // Internally appends chip-rate-rounded bits to the buffer; if this edge's
  // dt closes a burst (>6 ms gap), runs the framer.
  Status ingest_edge(uint32_t timestamp_us, bool level_after);

  // Feed a single chip directly. Each call adds exactly one bit to the
  // buffer at chip-rate time; the framer is checked on every word-boundary
  // (every 26 bits added). Used by host tests that want to bypass timing.
  Status ingest_chip(bool chip);

  const DecodedPacket &get_packet() const { return last_packet_; }

  // Diagnostics — not consumed by callers, just for log/test introspection.
  uint32_t total_edges() const { return total_edges_; }
  uint32_t chips_ingested() const { return chips_ingested_; }
  uint32_t bursts_decoded() const { return bursts_decoded_; }
  uint32_t bursts_failed() const { return bursts_failed_; }
  uint32_t packets_emitted() const { return packets_emitted_; }
  uint32_t buffer_overflows() const { return buffer_overflows_; }

  // Helpers exposed for tests + ECC inversion reuse.
  static uint8_t parity_9(uint16_t data_with_pad);
  static uint8_t compute_checksum(uint8_t cmd, uint8_t c, uint8_t d);
  static void invert_checksum(uint8_t cmd, uint8_t chk, uint8_t &c, uint8_t &d);

 private:
  // Chip bit buffer (chip-rate-locked sample stream).
  uint8_t bit_buffer_[kBitBufferBytes]{};
  size_t bit_count_{0};
  // Lower bound for scan offsets — advanced past each matched packet so
  // ingest_chip's word-boundary re-scans don't re-emit the same packet.
  // Reset to 0 on every clear_buffer_() (burst end / overflow).
  size_t scan_start_offset_{0};

  // Edge-to-chip-count state.
  bool have_last_edge_{false};
  uint32_t last_edge_us_{0};
  bool level_at_last_edge_{false};

  // Decoded result.
  DecodedPacket last_packet_{};

  // Diagnostics.
  uint32_t total_edges_{0};
  uint32_t chips_ingested_{0};
  uint32_t bursts_decoded_{0};   // burst with at least one valid packet
  uint32_t bursts_failed_{0};    // burst that didn't yield a packet
  uint32_t packets_emitted_{0};
  uint32_t buffer_overflows_{0};

  // Internal helpers.
  void clear_buffer_();
  bool get_bit_(size_t index) const;
  void set_bit_(size_t index, bool value);
  void append_chips_(bool level, uint32_t count);
  bool try_decode_packet_at_(size_t start);
  Status scan_framer_();
};

}  // namespace proflame2
}  // namespace esphome
