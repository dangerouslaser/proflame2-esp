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

  const size_t head = self->rx_ring_head_;
  const size_t next = (head + 1) % kRxRingSize;
  if (next == self->rx_ring_tail_) {
    // Ring full — count drop, leave existing data in place. Better to lose
    // a recent edge than corrupt the in-flight packet for the consumer.
    self->rx_overflow_count_++;
    return;
  }
  self->rx_ring_[head] = {now, level};
  self->rx_ring_head_ = next;
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

  // Drop any stale ring contents from a prior session.
  this->rx_ring_head_ = 0;
  this->rx_ring_tail_ = 0;
  this->rx_overflow_count_ = 0;
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

  const size_t pending = (this->rx_ring_head_ + kRxRingSize -
                          this->rx_ring_tail_) %
                         kRxRingSize;
  ESP_LOGI(TAG_RX, "RX capture stopped (pending=%u, overflows=%u)",
           static_cast<unsigned>(pending),
           static_cast<unsigned>(this->rx_overflow_count_));
}

void ProFlame2Component::service_rx_() {
  // Drain the ring. When a learn flow is in progress, feed every edge into
  // the decoder; on a successful packet, hand it to the learn state machine.
  // Outside learn-mode (e.g. a hypothetical future debug-only RX) the per-
  // edge VERBOSE log still surfaces signal-quality info.
  while (this->rx_ring_tail_ != this->rx_ring_head_) {
    const auto &p = this->rx_ring_[this->rx_ring_tail_];
    const uint32_t dt = p.timestamp_us - this->rx_last_pulse_us_;
    this->rx_last_pulse_us_ = p.timestamp_us;

    if (this->learn_state_ != LearnState::kIdle) {
      const auto status =
          this->learn_decoder_.ingest_edge(p.timestamp_us, p.level);
      if (status == ProFlame2Decoder::Status::kPacketReady) {
        this->on_packet_decoded_(this->learn_decoder_.get_packet());
      }
    }

    ESP_LOGV(TAG_RX, "edge: dt=%u us level=%d", static_cast<unsigned>(dt),
             p.level ? 1 : 0);
    this->rx_ring_tail_ = (this->rx_ring_tail_ + 1) % kRxRingSize;
  }

  // Rate-limited overflow warning — don't spam if RX is mis-tuned.
  if (this->rx_overflow_count_ > 0) {
    static uint32_t last_warn_ms = 0;
    const uint32_t now = millis();
    if (now - last_warn_ms > 1000) {
      ESP_LOGW(TAG_RX, "RX ring overflow: %u dropped pulses",
               static_cast<unsigned>(this->rx_overflow_count_));
      this->rx_overflow_count_ = 0;
      last_warn_ms = now;
    }
  }
}

}  // namespace proflame2
}  // namespace esphome
