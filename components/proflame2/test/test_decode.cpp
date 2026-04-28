// Host-runnable tests for proflame2_decode. No ESPHome dependency. Built by
// the sibling CMakeLists.txt — no other tests in this repo, so we roll our
// own micro-harness rather than pull in GoogleTest just to assert.
//
// Coverage:
//   - chip-level round-trip on a known good packet (serial + ECC recovered)
//   - decoder rejects when one bit is flipped (parity catches it)
//   - decoder rejects when one chip is dropped (Manchester violation)
//   - two repeated packets in the same chip stream both decode
//   - timing-driven path: chips → edges with realistic ±40% jitter, decoded
//
// Tests use the existing public API plus the static helpers that the device
// component already relies on (compute_checksum / parity_9).

#include "proflame2_decode.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <utility>
#include <random>

using esphome::proflame2::DecodedPacket;
using esphome::proflame2::ProFlame2Decoder;

#define TEST_FAIL(fmt, ...)                                                    \
  do {                                                                         \
    std::fprintf(stderr, "FAIL %s:%d: " fmt "\n", __FILE__, __LINE__,          \
                 ##__VA_ARGS__);                                               \
    std::exit(1);                                                              \
  } while (0)

#define EXPECT(cond, fmt, ...)                                                 \
  do {                                                                         \
    if (!(cond)) {                                                             \
      TEST_FAIL("%s — " fmt, #cond, ##__VA_ARGS__);                            \
    }                                                                          \
  } while (0)

static int g_test_count = 0;
static const char *g_current_test = "";

#define TEST(name)                                                             \
  static void name();                                                          \
  static struct Reg_##name {                                                   \
    Reg_##name() { run_test(#name, name); }                                    \
  } reg_##name;                                                                \
  static void name()

namespace {

void run_test(const char *name, void (*body)()) {
  g_current_test = name;
  body();
  ++g_test_count;
  std::printf("PASS %s\n", name);
}

// ---- Test-side encoder (mirror of build_packet + encode_manchester) -------

void encode_word_chips(uint8_t data, bool pad, std::vector<bool> &out) {
  // Word layout (13 bits, MSB first): bit 12=S, bit 11=start_guard(1),
  // bits 10..3=data, bit 2=pad, bit 1=parity, bit 0=end_guard(1).
  const uint8_t parity = ProFlame2Decoder::parity_9(
      static_cast<uint16_t>(data) | (pad ? 0x100u : 0u));
  uint16_t bits12 =
      static_cast<uint16_t>((1u << 11) | (static_cast<uint16_t>(data) << 3) |
                            (pad ? (1u << 2) : 0u) |
                            (static_cast<uint16_t>(parity) << 1) | 1u);

  // S marker = "11"
  out.push_back(true);
  out.push_back(true);

  // 12 data bits, MSB first, each as Manchester pair.
  for (int i = 11; i >= 0; --i) {
    const bool b = (bits12 >> i) & 1u;
    if (b) {
      out.push_back(true);   // "10"
      out.push_back(false);
    } else {
      out.push_back(false);  // "01"
      out.push_back(true);
    }
  }
}

std::vector<bool> encode_packet_chips(uint32_t serial, uint8_t cmd1,
                                      uint8_t cmd2, uint8_t c1, uint8_t d1,
                                      uint8_t c2, uint8_t d2) {
  std::vector<bool> chips;
  const uint8_t chk1 = ProFlame2Decoder::compute_checksum(cmd1, c1, d1);
  const uint8_t chk2 = ProFlame2Decoder::compute_checksum(cmd2, c2, d2);
  const uint8_t s1 = (serial >> 16) & 0xFF;
  const uint8_t s2 = (serial >> 8) & 0xFF;
  const uint8_t s3 = serial & 0xFF;
  encode_word_chips(s1, true, chips);
  encode_word_chips(s2, false, chips);
  encode_word_chips(s3, false, chips);
  encode_word_chips(cmd1, false, chips);
  encode_word_chips(cmd2, false, chips);
  encode_word_chips(chk1, false, chips);
  encode_word_chips(chk2, false, chips);
  return chips;
}

// Convert a chip stream into edge timestamps, one per level transition.
// jitter_us is added to each edge's timestamp from a uniform [-jitter, +jitter]
// distribution to simulate ISR latency.
struct Edge {
  uint32_t t_us;
  bool level_after;
};

std::vector<Edge> chips_to_edges(const std::vector<bool> &chips,
                                 uint32_t start_us = 1000,
                                 uint32_t jitter_us = 0,
                                 unsigned seed = 0x5EED) {
  std::vector<Edge> edges;
  std::mt19937 rng(seed);
  std::uniform_int_distribution<int> jitter(-static_cast<int>(jitter_us),
                                            static_cast<int>(jitter_us));
  bool prev = false;  // assume LOW pre-packet
  uint32_t t = start_us;
  for (bool c : chips) {
    if (c != prev) {
      const int j = (jitter_us == 0) ? 0 : jitter(rng);
      edges.push_back({static_cast<uint32_t>(static_cast<int>(t) + j), c});
      prev = c;
    }
    t += ProFlame2Decoder::kHalfBitUs;
  }
  // Always emit a synthetic closing edge — the decoder needs an edge AFTER
  // the last chip's slot to compute its duration. In real RF, the carrier
  // staying LOW is "no edge", but the decoder also has a timeout that
  // achieves the same effect; the synthetic edge here just makes the test
  // deterministic.
  edges.push_back({t, !prev});
  return edges;
}

}  // namespace

// =========================================================================
//   Tests
// =========================================================================

TEST(round_trip_chip_level) {
  ProFlame2Decoder d;
  // Bryan's pairing values — the dangerouslaser default.
  const uint32_t serial = 0x320A02u;
  const uint8_t cmd1 = 0x83;
  const uint8_t cmd2 = 0x40;
  const uint8_t c1 = 0x08, d1 = 0x0E;
  const uint8_t c2 = 0x0B, d2 = 0x07;

  const auto chips = encode_packet_chips(serial, cmd1, cmd2, c1, d1, c2, d2);
  ProFlame2Decoder::Status st = ProFlame2Decoder::Status::kPending;
  for (bool c : chips) {
    st = d.ingest_chip(c);
    if (st == ProFlame2Decoder::Status::kPacketReady) break;
  }
  EXPECT(st == ProFlame2Decoder::Status::kPacketReady,
         "decoder did not produce a packet");
  const DecodedPacket &p = d.get_packet();
  EXPECT(p.serial == serial, "serial mismatch: got 0x%06X expected 0x%06X",
         p.serial, serial);
  EXPECT(p.cmd1 == cmd1, "cmd1 mismatch");
  EXPECT(p.cmd2 == cmd2, "cmd2 mismatch");
  EXPECT(p.c1 == c1, "c1 mismatch: got 0x%X expected 0x%X", p.c1, c1);
  EXPECT(p.d1 == d1, "d1 mismatch");
  EXPECT(p.c2 == c2, "c2 mismatch");
  EXPECT(p.d2 == d2, "d2 mismatch");
}

TEST(rejects_bit_flip) {
  ProFlame2Decoder d;
  auto chips = encode_packet_chips(0x320A02u, 0x83, 0x40, 0x08, 0x0E, 0x0B,
                                   0x07);
  // Flip the first chip of the second word's data byte (well into the
  // packet — past the S marker, well before the trailing words). This
  // breaks the Manchester pair → bad bit → parity violation → reset.
  // First word ends after chip 26, so chip 30 is mid-word-2.
  chips[30] = !chips[30];

  ProFlame2Decoder::Status st = ProFlame2Decoder::Status::kPending;
  for (bool c : chips) {
    st = d.ingest_chip(c);
    if (st == ProFlame2Decoder::Status::kPacketReady) break;
  }
  EXPECT(st != ProFlame2Decoder::Status::kPacketReady,
         "bit-flipped packet incorrectly accepted");
}

TEST(rejects_dropped_chip) {
  ProFlame2Decoder d;
  auto chips = encode_packet_chips(0x320A02u, 0x83, 0x40, 0x08, 0x0E, 0x0B,
                                   0x07);
  chips.erase(chips.begin() + 30);  // drop a chip mid-packet

  ProFlame2Decoder::Status st = ProFlame2Decoder::Status::kPending;
  for (bool c : chips) {
    st = d.ingest_chip(c);
    if (st == ProFlame2Decoder::Status::kPacketReady) break;
  }
  EXPECT(st != ProFlame2Decoder::Status::kPacketReady,
         "dropped-chip packet incorrectly accepted");
}

TEST(decodes_two_repeats_in_stream) {
  ProFlame2Decoder d;
  auto p1 = encode_packet_chips(0x320A02u, 0x83, 0x40, 0x08, 0x0E, 0x0B, 0x07);
  auto p2 = encode_packet_chips(0x320A02u, 0x84, 0x41, 0x08, 0x0E, 0x0B, 0x07);

  std::vector<bool> stream;
  stream.insert(stream.end(), p1.begin(), p1.end());
  stream.insert(stream.end(), p2.begin(), p2.end());

  int packets_seen = 0;
  uint32_t last_cmd1 = 0;
  for (bool c : stream) {
    const auto st = d.ingest_chip(c);
    if (st == ProFlame2Decoder::Status::kPacketReady) {
      ++packets_seen;
      last_cmd1 = d.get_packet().cmd1;
      if (packets_seen == 1) {
        EXPECT(last_cmd1 == 0x83, "first packet cmd1 wrong");
      }
    }
  }
  EXPECT(packets_seen == 2, "expected 2 packets, got %d", packets_seen);
  EXPECT(last_cmd1 == 0x84, "second packet cmd1 wrong");
}

TEST(round_trip_via_edges_no_jitter) {
  ProFlame2Decoder d;
  const uint32_t serial = 0xDEADBEu & 0x00FFFFFFu;
  const auto chips = encode_packet_chips(serial, 0x83, 0x40, 0x05, 0x0A, 0x03,
                                         0x0C);
  const auto edges = chips_to_edges(chips, /*start_us=*/2000);

  ProFlame2Decoder::Status st = ProFlame2Decoder::Status::kPending;
  for (const auto &e : edges) {
    st = d.ingest_edge(e.t_us, e.level_after);
    if (st == ProFlame2Decoder::Status::kPacketReady) break;
  }
  EXPECT(st == ProFlame2Decoder::Status::kPacketReady,
         "edge-mode decode failed");
  EXPECT(d.get_packet().serial == serial, "serial mismatch via edges");
  EXPECT(d.get_packet().c1 == 0x05, "c1 mismatch via edges");
  EXPECT(d.get_packet().d1 == 0x0A, "d1 mismatch via edges");
}

TEST(round_trip_via_edges_with_jitter) {
  ProFlame2Decoder d;
  const auto chips = encode_packet_chips(0x123456u, 0xA1, 0x5C, 0x04, 0x09,
                                         0x0E, 0x02);
  // ±40 µs per edge — adjacent edges' jitter compounds to ±80 µs dt jitter,
  // which stays well inside the ±83 µs (40% of half-bit) tolerance budget.
  // Realistic ESP32-S3 ISR latency is typically ≤50 µs even under WiFi load.
  const auto edges = chips_to_edges(chips, /*start_us=*/2000,
                                    /*jitter_us=*/40);

  ProFlame2Decoder::Status st = ProFlame2Decoder::Status::kPending;
  for (const auto &e : edges) {
    st = d.ingest_edge(e.t_us, e.level_after);
    if (st == ProFlame2Decoder::Status::kPacketReady) break;
  }
  EXPECT(st == ProFlame2Decoder::Status::kPacketReady,
         "edge-mode decode failed under realistic jitter");
}

TEST(ecc_inversion_is_self_inverse) {
  // For every (cmd, c, d) with c, d in 0..15: encode, then invert, expect
  // identity. Brute-force verification of the algebraic claim used to
  // shortcut RX learning.
  for (uint32_t cmd = 0; cmd < 256; ++cmd) {
    for (uint8_t c = 0; c < 16; ++c) {
      for (uint8_t d = 0; d < 16; ++d) {
        const uint8_t chk =
            ProFlame2Decoder::compute_checksum(static_cast<uint8_t>(cmd), c, d);
        uint8_t got_c = 0xFF, got_d = 0xFF;
        ProFlame2Decoder::invert_checksum(static_cast<uint8_t>(cmd), chk,
                                          got_c, got_d);
        EXPECT(got_c == c && got_d == d,
               "invert mismatch cmd=0x%02X c=0x%X d=0x%X chk=0x%02X "
               "→ got c=0x%X d=0x%X",
               cmd, c, d, chk, got_c, got_d);
      }
    }
  }
}

int main() {
  // Tests register themselves at static init; main() just summarizes.
  std::printf("\n%d test(s) passed\n", g_test_count);
  return 0;
}
