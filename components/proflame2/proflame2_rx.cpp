#include "proflame2_cc1101.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/gpio.h"

namespace esphome {
namespace proflame2 {

static const char *const TAG_RX = "proflame2.rx";

// IRAM_ATTR: must remain resident during flash erase/write so the handler can
// fire while WiFi/OTA is touching flash. Body is intentionally minimal — push
// to ring and return. All decoding/logging happens in service_rx_().
void IRAM_ATTR ProFlame2Component::rx_isr_(ProFlame2Component *self) {
  if (!self->rx_active_) {
    return;
  }
  const uint32_t now = micros();
  const bool level = self->gdo0_isr_pin_.digital_read();

  // Producer: relaxed load of own head (no other writer); acquire load of
  // tail so we observe the consumer's drain progress.
  const size_t head = self->rx_ring_head_.load(std::memory_order_relaxed);
  const size_t next = (head + 1) % kRxRingSize;
  if (next == self->rx_ring_tail_.load(std::memory_order_acquire)) {
    // Ring full — count drop, leave existing data in place. Better to lose
    // a recent edge than corrupt the in-flight packet for the consumer.
    self->rx_overflow_count_.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  self->rx_ring_[head] = {now, level};
  // Release store: pairs with the consumer's acquire load on head so the
  // slot's payload is visible before the bumped index.
  self->rx_ring_head_.store(next, std::memory_order_release);
}

void ProFlame2Component::start_rx_capture() {
  if (this->rx_active_) {
    return;  // idempotent
  }
  if (this->tx_state_ != TX_IDLE || this->send_pending_ ||
      this->tx_repeat_left_ != 0) {
    ESP_LOGW(TAG_RX, "Refusing RX start — TX in flight");
    return;
  }
  if (this->gdo0_pin_ == nullptr) {
    ESP_LOGE(TAG_RX, "GDO0 pin not configured; RX impossible");
    return;
  }

  // Drop any stale ring contents from a prior session. RX is not active yet
  // (rx_active_ flips below), so the ISR is gated out — relaxed stores OK.
  this->rx_ring_head_.store(0, std::memory_order_relaxed);
  this->rx_ring_tail_.store(0, std::memory_order_relaxed);
  this->rx_overflow_count_.store(0, std::memory_order_relaxed);
  this->rx_last_pulse_us_ = 0;

  // Switch CC1101 register set to RX. Re-pin GDO0 as INPUT — boot configured
  // it as OUTPUT (TX FIFO threshold). Now it's CC1101's demodulated bit
  // stream.
  this->set_radio_mode_(RadioMode::kRx);
  this->gdo0_pin_->pin_mode(gpio::FLAG_INPUT);

  if (!this->rx_isr_attached_) {
    auto *internal = static_cast<InternalGPIOPin *>(this->gdo0_pin_);
    this->gdo0_isr_pin_ = internal->to_isr();
    internal->attach_interrupt(rx_isr_, this, gpio::INTERRUPT_ANY_EDGE);
    this->rx_isr_attached_ = true;
  }

  this->rx_active_ = true;
  this->send_strobe(CC1101_SRX);

  // Give the chip a beat to enter RX, then dump key registers + RSSI so we
  // can confirm what's actually loaded vs what we wrote, and whether it's
  // hearing anything on the antenna.
#ifdef USE_ESP_IDF
  vTaskDelay(pdMS_TO_TICKS(2));
#else
  delay(2);
#endif
  const uint8_t marcstate = this->read_status_register(0x35) & 0x1F;
  const uint8_t rssi_raw = this->read_status_register(0x34);
  const int rssi_dbm = (rssi_raw >= 128 ? (rssi_raw - 256) : rssi_raw) / 2 - 74;
  ESP_LOGI(TAG_RX,
           "MARCSTATE=0x%02X RSSI=%d dBm  IOCFG0=0x%02X PKTCTRL0=0x%02X "
           "MDMCFG2=0x%02X AGCCTRL2=0x%02X AGCCTRL1=0x%02X AGCCTRL0=0x%02X",
           marcstate, rssi_dbm, this->read_register(CC1101_IOCFG0),
           this->read_register(CC1101_PKTCTRL0),
           this->read_register(CC1101_MDMCFG2),
           this->read_register(CC1101_AGCCTRL2),
           this->read_register(CC1101_AGCCTRL1),
           this->read_register(CC1101_AGCCTRL0));

  ESP_LOGI(TAG_RX, "RX capture started");
}

void ProFlame2Component::stop_rx_capture() {
  if (!this->rx_active_) {
    return;
  }
  // ISR self-gates on rx_active_, so flipping the flag stops new ring writes
  // immediately. Detaching the interrupt cleanly mid-edge is fiddly and the
  // gate is one comparison.
  this->rx_active_ = false;
  this->set_radio_mode_(RadioMode::kIdle);

  // ISR is gated by rx_active_; relaxed loads are sufficient for the diag.
  const size_t head = this->rx_ring_head_.load(std::memory_order_relaxed);
  const size_t tail = this->rx_ring_tail_.load(std::memory_order_relaxed);
  const size_t pending = (head + kRxRingSize - tail) % kRxRingSize;
  ESP_LOGI(TAG_RX, "RX capture stopped (pending=%u, overflows=%u)",
           static_cast<unsigned>(pending),
           static_cast<unsigned>(
               this->rx_overflow_count_.load(std::memory_order_relaxed)));
}

void ProFlame2Component::service_rx_() {
  // Per-call stats so we can periodically summarize signal quality without
  // flooding the API log with per-edge messages.
  static uint32_t window_start_ms = 0;
  static uint32_t edges_in_window = 0;
  static uint32_t bucket_short = 0;   // 100..300 µs — single Manchester half-bit
  static uint32_t bucket_long = 0;    // 300..600 µs — double half-bit
  static uint32_t bucket_other = 0;   // anything else (mostly noise / silence)
  // Decoder counter snapshots — diff against current to show per-window deltas.
  static uint32_t last_chips_ = 0;
  static uint32_t last_pkts_ = 0;
  static uint32_t last_bursts_ok_ = 0;
  static uint32_t last_bursts_fail_ = 0;
  static uint32_t last_overflows_ = 0;

  // Drain the ring. When a learn flow is in progress, feed every edge into
  // the decoder; on a successful packet, hand it to the learn state machine.
  // Consumer: acquire load of head pairs with the ISR's release store so we
  // see the slot payload before the index. Tail is our own — relaxed.
  size_t tail = this->rx_ring_tail_.load(std::memory_order_relaxed);
  while (tail != this->rx_ring_head_.load(std::memory_order_acquire)) {
    const auto &p = this->rx_ring_[tail];
    const uint32_t dt = p.timestamp_us - this->rx_last_pulse_us_;
    this->rx_last_pulse_us_ = p.timestamp_us;

    if (this->learn_state_ != LearnState::kIdle) {
      const auto status =
          this->learn_decoder_.ingest_edge(p.timestamp_us, p.level);
      if (status == ProFlame2Decoder::Status::kPacketReady) {
        this->on_packet_decoded_(this->learn_decoder_.get_packet());
      }
    }

    edges_in_window++;
    if (dt >= 100 && dt <= 300) {
      bucket_short++;
    } else if (dt > 300 && dt <= 600) {
      bucket_long++;
    } else {
      bucket_other++;
    }

    tail = (tail + 1) % kRxRingSize;
    // Release store: the ISR's acquire load of tail observes our drain
    // progress and stops treating the slot we just consumed as occupied.
    this->rx_ring_tail_.store(tail, std::memory_order_release);
  }

  // Periodic stats. Healthy ProFlame demodulation looks like a burst with
  // many short+long buckets clustered around 200/400 µs. Lots of "other"
  // (>600 µs gaps) with a sprinkle of short/long is noise — AGC chatter
  // without real signal.
  const uint32_t now = millis();
  if (window_start_ms == 0) {
    window_start_ms = now;
  }
  if (now - window_start_ms >= 500) {
    if (edges_in_window > 0 && this->learn_state_ != LearnState::kIdle) {
      const uint32_t chips = this->learn_decoder_.chips_ingested();
      const uint32_t pkts = this->learn_decoder_.packets_emitted();
      const uint32_t bursts_ok = this->learn_decoder_.bursts_decoded();
      const uint32_t bursts_fail = this->learn_decoder_.bursts_failed();
      const uint32_t overflows = this->learn_decoder_.buffer_overflows();
      ESP_LOGI(TAG_RX, "RX 500ms: edges=%u short=%u long=%u other=%u",
               static_cast<unsigned>(edges_in_window),
               static_cast<unsigned>(bucket_short),
               static_cast<unsigned>(bucket_long),
               static_cast<unsigned>(bucket_other));
      ESP_LOGI(TAG_RX,
               "  decode: chips=%u pkts=%u bursts_ok=%u bursts_fail=%u overflows=%u",
               static_cast<unsigned>(chips - last_chips_),
               static_cast<unsigned>(pkts - last_pkts_),
               static_cast<unsigned>(bursts_ok - last_bursts_ok_),
               static_cast<unsigned>(bursts_fail - last_bursts_fail_),
               static_cast<unsigned>(overflows - last_overflows_));
      last_chips_ = chips;
      last_pkts_ = pkts;
      last_bursts_ok_ = bursts_ok;
      last_bursts_fail_ = bursts_fail;
      last_overflows_ = overflows;
    }
    window_start_ms = now;
    edges_in_window = bucket_short = bucket_long = bucket_other = 0;
  }

  // Rate-limited overflow warning — don't spam if RX is mis-tuned. Peek
  // first; only do the atomic exchange-and-reset once we've decided to log,
  // so quiet windows leave the counter alone for the next interval.
  if (this->rx_overflow_count_.load(std::memory_order_relaxed) > 0) {
    static uint32_t last_warn_ms = 0;
    if (now - last_warn_ms > 1000) {
      const uint32_t dropped =
          this->rx_overflow_count_.exchange(0, std::memory_order_relaxed);
      if (dropped > 0) {
        ESP_LOGW(TAG_RX, "RX ring overflow: %u dropped pulses",
                 static_cast<unsigned>(dropped));
      }
      last_warn_ms = now;
    }
  }
}

}  // namespace proflame2
}  // namespace esphome
