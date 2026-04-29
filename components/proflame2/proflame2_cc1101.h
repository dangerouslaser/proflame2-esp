#pragma once

// Intentionally avoid <esphome.h> here: the aggregate header drags in every
// component (including proflame2_climate.h), which would create a circular
// include chain because proflame2_climate.h depends on this file's
// ProFlame2Component definition. Use explicit per-component includes instead.
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/core/hal.h"
#include "esphome/components/spi/spi.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/number/number.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/preferences.h"
#include "proflame2_cc1101_regs.h"
#include "proflame2_decode.h"
#include <atomic>
#include <cstdint>

namespace esphome {
namespace proflame2 {

class ProFlame2Climate;  // forward decl — defined in proflame2_climate.h

// Radio mode — selects which CC1101 register subset is applied. The chip
// itself has more states (FSTXON, CALIBRATE, etc.); we only ever drive it
// between idle, transmit-ready, and receive-ready.
enum class RadioMode : uint8_t { kIdle, kTx, kRx };

// Single edge captured by the RX ISR. timestamp_us comes from micros() at the
// time of the edge; level is the GDO0 logic level *after* the edge.
struct ProFlame2RxPulse {
  uint32_t timestamp_us;
  bool level;
};

// ProFlame 2 packet structure
struct ProFlame2Command {
  bool pilot_cpi;        // 0=IPI, 1=CPI
  uint8_t light_level;   // 0-6
  bool thermostat;
  bool power;

  bool secondary_flame;
  uint8_t fan_level;     // 0-6
  bool aux_power;
  uint8_t flame_level;   // 0-6
};

// Persistent state written by the on-device learn-mode pairing flow. When a
// valid blob is present in NVS, its serial + ECC constants override the
// YAML-seeded defaults at boot. See README "Pairing your remote".
struct ProFlame2LearnedState {
  static constexpr uint16_t kCurrentVersion = 1;
  static constexpr uint16_t kFlagValid = 0x0001;

  uint16_t version;
  uint16_t flags;
  uint32_t serial;          // 24-bit serial in low bits
  uint8_t  c1, d1, c2, d2;  // ECC constants, low 4 bits used
  uint8_t  reserved[6];     // forward-compat slack
  uint32_t crc32;           // CRC-32/ISO-HDLC over all preceding bytes
} __attribute__((packed));
static_assert(sizeof(ProFlame2LearnedState) == 22,
              "ProFlame2LearnedState layout must remain 22 bytes for NVS compat");

class ProFlame2Component : public Component,
                           public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST,
                                                 spi::CLOCK_POLARITY_LOW,
                                                 spi::CLOCK_PHASE_LEADING,
                                                 spi::DATA_RATE_1MHZ> {
 public:
  ProFlame2Component() {}

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  // Configuration methods
  void set_serial_number(uint32_t serial) { this->serial_number_ = serial; }
  void set_gdo0_pin(GPIOPin *pin) { this->gdo0_pin_ = pin; }

  // Board-quirk pins for the LilyGo T-Embed CC1101. Optional — generic ESP32 +
  // breakout boards leave these unset and the component skips them.
  //   pwr_en_pin  : GPIO15 on T-Embed. Drive HIGH to power the CC1101 rail.
  //                 Without this, all SPI register reads return 0x00.
  //   ant_sw0_pin : GPIO48 on T-Embed. RF antenna switch control 0.
  //   ant_sw1_pin : GPIO47 on T-Embed. RF antenna switch control 1.
  // For the 314.973 MHz ProFlame band: SW1=HIGH, SW0=LOW (315 MHz routing).
  void set_pwr_en_pin(GPIOPin *pin) { this->pwr_en_pin_ = pin; }
  void set_ant_sw0_pin(GPIOPin *pin) { this->ant_sw0_pin_ = pin; }
  void set_ant_sw1_pin(GPIOPin *pin) { this->ant_sw1_pin_ = pin; }
  // ECC pairing constants. (c1, d1) are used to compute err1 from cmd1,
  // (c2, d2) are used to compute err2 from cmd2. These are remote-specific —
  // a different fireplace pairing will require different values. See README.
  void set_ecc_constants(uint8_t c1, uint8_t d1, uint8_t c2, uint8_t d2) {
    this->ecc_c1_ = c1 & 0x0F;
    this->ecc_d1_ = d1 & 0x0F;
    this->ecc_c2_ = c2 & 0x0F;
    this->ecc_d2_ = d2 & 0x0F;
  }

  // Control methods
  void set_power(bool state);
  void set_pilot_mode(bool cpi_mode);
  void set_flame_level(uint8_t level);
  void set_fan_level(uint8_t level);
  void set_light_level(uint8_t level);
  void set_aux_power(bool state);
  void set_secondary_flame(bool state);
  void set_thermostat(bool state);
  void queue_send();

  // Switch components
  void set_power_switch(switch_::Switch *sw) { this->power_switch_ = sw; }
  void set_pilot_switch(switch_::Switch *sw) { this->pilot_switch_ = sw; }
  void set_aux_switch(switch_::Switch *sw) { this->aux_switch_ = sw; }
  void set_secondary_flame_switch(switch_::Switch *sw) {
    this->secondary_flame_switch_ = sw;
  }
  void set_thermostat_switch(switch_::Switch *sw) {
    this->thermostat_switch_ = sw;
  }

  // Number components for levels
  void set_flame_number(number::Number *num) { this->flame_number_ = num; }
  void set_fan_number(number::Number *num) { this->fan_number_ = num; }
  // Light entity (replaces the old light Number); set by codegen so the parent
  // can force the light off when the fireplace powers down.
  void set_light_state(light::LightState *state) { this->light_state_ = state; }
  // Climate entity (optional). Lets the parent notify climate of manual
  // power/mode changes so it can re-publish action.
  void set_climate(ProFlame2Climate *clim) { this->climate_ = clim; }

  // Read-only diagnostic text sensors that surface the active pairing
  // identity (serial, ECC constants, source) to Home Assistant. All three
  // are optional; whichever are wired get populated by
  // publish_diagnostic_sensors_() at setup() and after each learn commit.
  void set_serial_number_sensor(text_sensor::TextSensor *ts) {
    this->serial_number_sensor_ = ts;
  }
  void set_ecc_constants_sensor(text_sensor::TextSensor *ts) {
    this->ecc_constants_sensor_ = ts;
  }
  void set_pairing_source_sensor(text_sensor::TextSensor *ts) {
    this->pairing_source_sensor_ = ts;
  }

  // RX capture (async serial mode). The ISR pushes (timestamp_us, level)
  // pairs into a lock-free ring buffer; service_rx_() drains it in loop()
  // and feeds the decoder when a learn flow is active. Refuses to start
  // while a TX is queued or in flight; idempotent.
  void start_rx_capture();
  void stop_rx_capture();
  bool is_rx_active() const { return this->rx_active_; }

  // ======== Learn-mode (on-device remote pairing) =======================
  enum class LearnState : uint8_t {
    kIdle,         // not capturing
    kListening,    // RX armed, no valid packet yet
    kCapturing,    // ≥1 valid packet captured, awaiting more
    kConverged,    // ≥3 consistent packets — awaiting user confirm
    kPersisted,    // saved to NVS — auto-clears back to kIdle after 2 s
    kFailed,       // timeout / mismatch — UI should prompt retry
  };

  struct LearnCandidate {
    uint32_t serial{0};
    uint8_t c1{0}, d1{0}, c2{0}, d2{0};
    uint8_t valid_packet_count{0};
  };

  // Begin learn: clears the candidate, starts RX, transitions to kListening.
  // No-op if already in any non-idle learn state — caller should learn_cancel
  // first.
  void learn_start();

  // Stops RX, returns to kIdle. Existing NVS values (if any) are NOT touched.
  void learn_cancel();

  // Commit the converged candidate to NVS. Only valid in kConverged; returns
  // false otherwise. Stops RX as a side effect; transient kPersisted state
  // auto-clears to kIdle after a short hold so the UI can render confirmation.
  bool learn_confirm();

  LearnState get_learn_state() const { return this->learn_state_; }
  const LearnCandidate &get_learn_candidate() const {
    return this->learn_candidate_;
  }

  // How many packets must agree before the candidate is presented to the
  // user for confirmation. Public so the UI can include it in its progress
  // indicator.
  static constexpr uint8_t kLearnMinPackets = 3;

  // Where the active serial + ECC came from. UI uses this on the info screen.
  enum class ConfigSource : uint8_t { kYaml, kNvsLearned };
  ConfigSource get_config_source() const { return this->config_source_; }
  uint32_t get_serial_number() const { return this->serial_number_; }
  void get_ecc(uint8_t &c1, uint8_t &d1, uint8_t &c2, uint8_t &d2) const {
    c1 = this->ecc_c1_;
    d1 = this->ecc_d1_;
    c2 = this->ecc_c2_;
    d2 = this->ecc_d2_;
  }

  // DEBUG FUNCTIONS
  void debug_minimal_tx();
  void debug_check_config();

  // moved from protected for debugging in yaml
  void send_strobe(uint8_t strobe);
  void write_register(uint8_t reg, uint8_t value);
  uint8_t read_status_register(uint8_t reg);
  uint8_t read_register(uint8_t reg);

  ProFlame2Command current_state_{};
  // Last non-zero light level seen while power was on, OR a level the
  // user pre-dialed via the UI while power was off. Restored by
  // set_power(true) so toggling the fireplace doesn't lose the user's
  // last brightness preference. Exposed via get_remembered_light_level()
  // so the UI can display "LIGHT 4" while editing in the off state.
  uint8_t remembered_light_level_{0};
  uint8_t get_remembered_light_level() const {
    return this->remembered_light_level_;
  }
  // Mirror of remembered_light_level_ for the binary secondary-flame
  // setting. Defaults to true so a fresh boot's first power-on lights
  // both burners as before. Updated by set_secondary_flame() while
  // power is on (when ON) or while power is off (any change), and
  // applied by set_power(true).
  bool remembered_secondary_flame_{true};
  bool get_remembered_secondary_flame() const {
    return this->remembered_secondary_flame_;
  }
  void transmit_command();
  void build_packet(uint8_t *packet);
  void encode_manchester(uint8_t *input, uint8_t *output, size_t input_len);

 protected:
  void reset_cc1101();
  void configure_cc1101();
  // Equivalent to the radio-init sequence in setup(): SRES → configure_cc1101
  // → PA-table write. Use after learn-mode (which leaves RX-only registers
  // — FOCCFG, AGCCTRL2/1/0 — at envelope-detector values that the TX table
  // doesn't reset) so the next transmit is on a clean register set.
  void reinit_radio_();

  uint8_t calculate_checksum(uint8_t cmd_byte, uint8_t c_const, uint8_t d_const);
  uint8_t calculate_parity(uint16_t data);

  // NVS-backed learned-state load. Returns true if a valid blob was found and
  // its serial + ECC values applied (overriding YAML defaults).
  bool load_learned_state_();

  // Publish the current serial / ECC / source to the diagnostic text
  // sensors (if wired). Safe to call any time; no-op for unset sensors.
  void publish_diagnostic_sensors_();

  // Switch the CC1101 between idle, TX-ready, and RX-ready register sets.
  // Idempotent — re-applying the current mode is a no-op. Always strobes SIDLE
  // first so callers don't need to worry about prior chip state. Does NOT
  // strobe STX or SRX; callers initiate the actual transmit/receive.
  void set_radio_mode_(RadioMode mode);

  // RX ISR + service routine (implemented in proflame2_rx.cpp). The ISR is a
  // free function pointer (signature dictated by attach_interrupt) so it must
  // be a static method; it gets `this` via the user-data argument.
  static void IRAM_ATTR rx_isr_(ProFlame2Component *self);
  void service_rx_();

  // Learn-mode helpers (implemented in proflame2_learn.cpp).
  void service_learn_();
  void on_packet_decoded_(const DecodedPacket &p);
  bool save_learned_state_(uint32_t serial, uint8_t c1, uint8_t d1,
                           uint8_t c2, uint8_t d2);

  // CRC-32/ISO-HDLC. Used by load_learned_state_ (validate) and
  // save_learned_state_ (stamp). Static so it has no `this` dependency.
  static uint32_t crc32_iso_(const uint8_t *data, size_t len);

  // Non-blocking TX state machine
  void start_tx_(const uint8_t *data, size_t len);
  void service_tx_();

  // Hardware pins
  GPIOPin *gdo0_pin_{nullptr};
  // T-Embed CC1101 board-quirk pins. nullptr on generic ESP32 + breakout
  // hardware where the CC1101 has direct power and a fixed antenna.
  GPIOPin *pwr_en_pin_{nullptr};
  GPIOPin *ant_sw0_pin_{nullptr};
  GPIOPin *ant_sw1_pin_{nullptr};

  // Configuration
  uint32_t serial_number_{0x12345678};
  // ECC pairing constants — defaults match the dangerouslaser pairing (serial
  // 0x320A02). YAML can override; on-device learn-mode (when paired) overrides
  // both via NVS. See README "Pairing your remote".
  uint8_t ecc_c1_{0x08};
  uint8_t ecc_d1_{0x0E};
  uint8_t ecc_c2_{0x0B};
  uint8_t ecc_d2_{0x07};

  // Resolved at setup() — distinguishes YAML-seeded values from values learned
  // on-device via the pairing flow and persisted to NVS. Enum is declared in
  // the public section above so other components (e.g. ProFlame2UI) can read
  // it via get_config_source().
  ConfigSource config_source_{ConfigSource::kYaml};
  ESPPreferenceObject pref_learned_;

  // Diagnostic text sensor exports (optional; nullptr when not wired in YAML).
  text_sensor::TextSensor *serial_number_sensor_{nullptr};
  text_sensor::TextSensor *ecc_constants_sensor_{nullptr};
  text_sensor::TextSensor *pairing_source_sensor_{nullptr};

  // Current CC1101 mode tracking. Initialized to kIdle; configure_cc1101()
  // sets it to kTx after the boot register write. Future RX driver flips
  // between kRx and kTx as needed.
  RadioMode radio_mode_{RadioMode::kIdle};

  // RX state — populated by start_rx_capture(); consumed by service_rx_().
  // The ring is a single-producer (ISR) / single-consumer (loop task) queue.
  // The indices are std::atomic so the ISR writing on one core and the loop
  // reading on the other core are properly synchronized — `volatile` was
  // only "atomic by accident" on single-core ESP32 and provides no memory
  // ordering guarantees against compiler reordering across the data write.
  // Pattern: producer (ISR) does relaxed-load tail / release-store head;
  // consumer (loop) does acquire-load head / relaxed-store tail. The release
  // on the head store pairs with the acquire on the head load to ensure the
  // ring slot's payload is visible to the consumer before it observes the
  // bumped head index.
  static constexpr size_t kRxRingSize = 1024;
  ProFlame2RxPulse rx_ring_[kRxRingSize]{};
  std::atomic<size_t> rx_ring_head_{0};
  std::atomic<size_t> rx_ring_tail_{0};
  std::atomic<uint32_t> rx_overflow_count_{0};
  ISRInternalGPIOPin gdo0_isr_pin_{};
  bool rx_isr_attached_{false};
  bool rx_active_{false};
  uint32_t rx_last_pulse_us_{0};

  // Learn-mode state machine.
  static constexpr uint32_t kLearnTimeoutMs = 60000;
  static constexpr uint32_t kLearnPersistedHoldMs = 2000;
  LearnState learn_state_{LearnState::kIdle};
  LearnCandidate learn_candidate_{};
  uint32_t learn_started_ms_{0};
  uint32_t learn_persisted_ms_{0};
  ProFlame2Decoder learn_decoder_{};

  // Component references
  switch_::Switch *power_switch_{nullptr};
  switch_::Switch *pilot_switch_{nullptr};
  switch_::Switch *aux_switch_{nullptr};
  switch_::Switch *secondary_flame_switch_{nullptr};
  switch_::Switch *thermostat_switch_{nullptr};

  number::Number *flame_number_{nullptr};
  number::Number *fan_number_{nullptr};
  light::LightState *light_state_{nullptr};
  ProFlame2Climate *climate_{nullptr};

  bool spi_ready_{false};

  // buffer_dirty_: tracks whether current_state_ has unsent changes.
  // Transmission is intentionally NOT triggered automatically on state changes —
  // an explicit queue_send() call (Send button) is required to prevent accidental
  // TX during rapid slider adjustments. Cleared together with send_pending_.
  bool buffer_dirty_{false};
  bool send_pending_{false};

  // Timing
  uint32_t last_transmission_{0};
  static const uint32_t MIN_TRANSMISSION_INTERVAL = 200;

  // TX state
  enum TxState : uint8_t { TX_IDLE = 0, TX_RUNNING = 1, TX_ERROR = 2 };
  TxState tx_state_{TX_IDLE};
  uint8_t tx_buf_[160]{};
  size_t tx_len_{0};
  size_t tx_pos_{0};
  uint32_t tx_start_ms_{0};
  bool tx_pending_{false};

  // Repeat handling
  static constexpr uint8_t TX_REPEAT_TARGET = 5;   // 5 repeats like the real remote
  static constexpr uint16_t TX_REPEAT_GAP_MS = 2;  // tighter gap to keep repeats in one burst
  uint8_t tx_repeat_left_{0};
  uint32_t tx_next_repeat_ms_{0};
};

}  // namespace proflame2
}  // namespace esphome
