#include "proflame2_cc1101.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

#ifdef USE_ESP_IDF
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define YIELD() taskYIELD()
#else
#define YIELD() yield()
#endif

namespace esphome {
namespace proflame2 {

static const char *TAG = "proflame2";

// CC1101 Configuration for 314.973 MHz OOK at 2400 baud
static const uint8_t CC1101_CONFIG[][2] = {
    {CC1101_IOCFG0, 0x02},   {CC1101_FIFOTHR, 0x47}, {CC1101_PKTLEN, 125},
    {CC1101_PKTCTRL1, 0x00}, {CC1101_PKTCTRL0, 0x00}, {CC1101_FSCTRL1, 0x06},
    {CC1101_FREQ2, 0x0C},    {CC1101_FREQ1, 0x1D},   {CC1101_FREQ0, 0x89},
    {CC1101_MDMCFG4, 0xF6},   {CC1101_MDMCFG3, 0x83}, {CC1101_MDMCFG2, 0x30},
    {CC1101_MDMCFG1, 0x00},  {CC1101_MDMCFG0, 0xF8}, {CC1101_DEVIATN, 0x00},
    {CC1101_MCSM1, 0x00},     {CC1101_MCSM0, 0x04},   {CC1101_FREND1, 0x56},
    {CC1101_FREND0, 0x11},    {CC1101_FSCAL3, 0xEA},  {CC1101_FSCAL2, 0x2A},
    {CC1101_FSCAL1, 0x00},    {CC1101_FSCAL0, 0x11},  {CC1101_TEST2, 0x81},
    {CC1101_TEST1, 0x35},     {CC1101_TEST0, 0x09},
};

void ProFlame2Component::setup() {
  ESP_LOGCONFIG(TAG, "Setting up ProFlame 2 CC1101...");

  this->spi_setup();
  this->spi_ready_ = true;

  if (this->gdo0_pin_ != nullptr) {
    this->gdo0_pin_->setup();
    this->gdo0_pin_->pin_mode(gpio::FLAG_INPUT);
  }

  this->reset_cc1101();

#ifdef USE_ESP_IDF
  vTaskDelay(pdMS_TO_TICKS(10));
#else
  delay(10);
#endif

  this->configure_cc1101();

  // PA table for OOK (OFF / ON)
  uint8_t pa_table[8] = {0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  ESP_LOGD(TAG, "Setting PA table for OOK");
  this->enable();
  this->write_byte(CC1101_PATABLE | 0x40);
  for (int i = 0; i < 8; i++) {
    this->write_byte(pa_table[i]);
  }
  this->disable();

  // init state
  this->current_state_ = ProFlame2Command{
      .pilot_cpi = false,
      .light_level = 0,
      .thermostat = false,
      .power = false,
      .secondary_flame = false,
      .fan_level = 0,
      .aux_power = false,
      .flame_level = 0};

  ESP_LOGCONFIG(TAG, "ProFlame 2 setup complete");
}

void ProFlame2Component::loop() {
  if (this->send_pending_) {
    const uint32_t now = millis();
    if (this->spi_ready_ && this->tx_state_ == TX_IDLE &&
        this->tx_repeat_left_ == 0 &&
        (now - this->last_transmission_ >= MIN_TRANSMISSION_INTERVAL)) {
      this->send_pending_ = false;
      this->buffer_dirty_ = false;
      this->transmit_command();
    }
  }

  // NOTE: tx_repeat_left_ is always 0 in normal operation — transmit_command()
  // uses a single contiguous burst for all repeats. Block retained for potential
  // future debug use.
  // If we're in repeat-gap waiting window, fire next repeat when time arrives
  if (this->tx_state_ == TX_IDLE && this->tx_repeat_left_ > 0) {
    const uint32_t now = millis();
    if (now >= this->tx_next_repeat_ms_) {
      // next repeat of the SAME tx_buf_ (already loaded)
      this->start_tx_(this->tx_buf_, this->tx_len_);
      // start_tx_ sets TX_RUNNING; service_tx_ will decrement repeat count on
      // completion
    }
  }

  if (this->tx_state_ == TX_RUNNING) {
    this->service_tx_();
    YIELD();
  } else if (this->tx_repeat_left_ > 0) {
    // Keep the repeat gap short; avoid 10 ms idle delays while waiting
#ifdef USE_ESP_IDF
    vTaskDelay(pdMS_TO_TICKS(1));
#else
    delay(1);
#endif
  } else {
#ifdef USE_ESP_IDF
    vTaskDelay(pdMS_TO_TICKS(10));
#else
    delay(10);
#endif
  }
}

void ProFlame2Component::dump_config() {
  ESP_LOGCONFIG(TAG, "ProFlame 2 CC1101:");
  if (!this->spi_ready_) {
    ESP_LOGW(TAG, "SPI not ready yet, skipping CC1101 reads");
    return;
  }
  ESP_LOGCONFIG(TAG, "  Serial Number: 0x%08X", this->serial_number_);
  if (this->gdo0_pin_ != nullptr) {
    LOG_PIN("  GDO0 Pin: ", this->gdo0_pin_);
  }

  const uint8_t partnum = this->read_status_register(0x30);
  const uint8_t version = this->read_status_register(0x31);
  ESP_LOGCONFIG(TAG, "  CC1101 Part Number: 0x%02X", partnum);
  ESP_LOGCONFIG(TAG, "  CC1101 Version: 0x%02X", version);

  const uint8_t f2 = this->read_register(CC1101_FREQ2);
  const uint8_t f1 = this->read_register(CC1101_FREQ1);
  const uint8_t f0 = this->read_register(CC1101_FREQ0);
  const uint8_t mdm2 = this->read_register(CC1101_MDMCFG2);
  const uint8_t pkt0 = this->read_register(CC1101_PKTCTRL0);
  const uint8_t fr0 = this->read_register(CC1101_FREND0);
  ESP_LOGCONFIG(TAG, "  FREQ: 0x%02X%02X%02X", f2, f1, f0);
  ESP_LOGCONFIG(TAG, "  MDMCFG2: 0x%02X (modulation/sync)", mdm2);
  ESP_LOGCONFIG(TAG, "  PKTCTRL0: 0x%02X (packet mode)", pkt0);
  ESP_LOGCONFIG(TAG, "  FREND0: 0x%02X (PA_POWER)", fr0);
}

void ProFlame2Component::write_register(uint8_t reg, uint8_t value) {
  this->enable();
  this->write_byte(reg);
  this->write_byte(value);
  this->disable();
}

uint8_t ProFlame2Component::read_register(uint8_t reg) {
  this->enable();
  this->write_byte(reg | 0x80);
  uint8_t value = this->read_byte();
  this->disable();
  return value;
}

uint8_t ProFlame2Component::read_status_register(uint8_t reg) {
  this->enable();
  this->write_byte(reg | 0xC0);
  uint8_t value = this->read_byte();
  this->disable();
  return value;
}

void ProFlame2Component::send_strobe(uint8_t strobe) {
  this->enable();
  this->write_byte(strobe);
  this->disable();
}

void ProFlame2Component::reset_cc1101() {
  delayMicroseconds(5);

  this->enable();
  delayMicroseconds(10);
  this->disable();
  delayMicroseconds(40);

  this->enable();
  this->write_byte(CC1101_SRES);
  this->disable();

#ifdef USE_ESP_IDF
  vTaskDelay(pdMS_TO_TICKS(2));
#else
  delay(2);
#endif
}

void ProFlame2Component::configure_cc1101() {
  for (size_t i = 0; i < sizeof(CC1101_CONFIG) / sizeof(CC1101_CONFIG[0]);
       i++) {
    this->write_register(CC1101_CONFIG[i][0], CC1101_CONFIG[i][1]);
  }

  this->send_strobe(CC1101_SIDLE);
  this->send_strobe(CC1101_SFTX);
  this->send_strobe(CC1101_SFRX);

  this->send_strobe(CC1101_SCAL);

  ESP_LOGD(TAG, "CC1101 configured for 314.973 MHz OOK at 2400 baud");
}

uint8_t ProFlame2Component::calculate_parity(uint16_t data) {
  uint8_t ones = 0;
  for (int i = 0; i < 9; i++) {
    if (data & (1 << i)) {
      ones++;
    }
  }
  return ones & 1;
}

uint8_t ProFlame2Component::calculate_checksum(uint8_t cmd_byte, uint8_t c_const,
                                               uint8_t d_const) {
  uint8_t high_nibble = (cmd_byte >> 4) & 0x0F;
  uint8_t low_nibble = cmd_byte & 0x0F;

  uint8_t x = (c_const ^ (high_nibble << 1) ^ high_nibble ^ (low_nibble << 1)) &
              0x0F;
  uint8_t y = (d_const ^ high_nibble ^ low_nibble) & 0x0F;

  return (x << 4) | y;
}

void ProFlame2Component::build_packet(uint8_t *packet) {
  memset(packet, 0, 12);

  uint8_t serial1 = (this->serial_number_ >> 16) & 0xFF;
  uint8_t serial2 = (this->serial_number_ >> 8) & 0xFF;
  uint8_t serial3 = this->serial_number_ & 0xFF;

  uint8_t cmd1 = (this->current_state_.pilot_cpi ? 0x80 : 0x00) |
                 ((this->current_state_.light_level & 0x07) << 4) |
                 (this->current_state_.thermostat ? 0x02 : 0x00) |
                 (this->current_state_.power ? 0x01 : 0x00);

  uint8_t cmd2 = (this->current_state_.secondary_flame ? 0x80 : 0x00) |
                 ((this->current_state_.fan_level & 0x07) << 4) |
                 (this->current_state_.aux_power ? 0x08 : 0x00) |
                 (this->current_state_.flame_level & 0x07);

  // uint8_t checksum1 = this->calculate_checksum(cmd1, 0x0D, 0x00);
  // uint8_t checksum2 = this->calculate_checksum(cmd2, 0x00, 0x07);
  // ECC tuned from real remote captures:
  //   cmd1=0x01 -> err1=0x73
  //   cmd2=0x31 -> err2=0x36
  //   cmd2=0x21 -> err2=0x07
  // These map to (c,d) = (0x05,0x02) for cmd1, (0x04,0x04) for cmd2.
  uint8_t checksum1 = this->calculate_checksum(cmd1, 0x05, 0x02);
  uint8_t checksum2 = this->calculate_checksum(cmd2, 0x04, 0x04);

  ESP_LOGD(TAG,
           "CMD summary: serial bytes=%02X %02X %02X cmd1=0x%02X cmd2=0x%02X "
           "err1=0x%02X err2=0x%02X",
           serial1, serial2,
           serial3, cmd1, cmd2, checksum1, checksum2);
  ESP_LOGD(TAG, "Serial full: 0x%08X", this->serial_number_);

  // Build 7 words, 13 bits each: S(1) + guard(1) + data(8) + pad(1) + parity(1) + end guard(1)
  // pad bit is 1 only on the first word; parity is over data+pad.
  uint16_t words[7];
  auto make_word = [&](uint8_t data, bool pad_bit) -> uint16_t {
    uint8_t parity =
        this->calculate_parity(static_cast<uint16_t>(data) | (pad_bit ? 0x100 : 0x000));
    uint16_t w = 0;
    w |= (1 << 12);                                // sync 'S'
    w |= (1 << 11);                                // start guard '1'
    w |= (static_cast<uint16_t>(data) << 3);       // data bits
    if (pad_bit) w |= (1 << 2);                    // pad bit
    w |= (static_cast<uint16_t>(parity) << 1);     // parity bit
    w |= 0x01;                                     // end guard '1'
    return w;
  };

  words[0] = make_word(serial1, true);   // pad=1 on first word
  words[1] = make_word(serial2, false);
  words[2] = make_word(serial3, false);
  words[3] = make_word(cmd1, false);
  words[4] = make_word(cmd2, false);
  words[5] = make_word(checksum1, false);
  words[6] = make_word(checksum2, false);

  int bit_index = 0;
  for (int w = 0; w < 7; w++) {
    for (int b = 12; b >= 0; b--) {
      int byte_index = bit_index / 8;
      int bit_offset = 7 - (bit_index % 8);
      if (words[w] & (1 << b)) {
        packet[byte_index] |= (1 << bit_offset);
      }
      bit_index++;
    }
  }
}

void ProFlame2Component::encode_manchester(uint8_t *input, uint8_t *output,
                                           size_t input_bits) {
  int out_index = 0;
  memset(output, 0, 23);

  for (size_t i = 0; i < input_bits; i++) {
    int byte_index = i / 8;
    int bit_offset = 7 - (i % 8);
    bool bit = (input[byte_index] >> bit_offset) & 1;

    bool is_sync = (i % 13) == 0;

    int out_byte = out_index / 8;
    int out_bit = 7 - (out_index % 8);

    if (is_sync) {
      output[out_byte] |= (1 << out_bit);
      out_index++;
      out_bit = 7 - (out_index % 8);
      if (out_bit == 7) {
        out_byte++;
      }
      output[out_byte] |= (1 << out_bit);
    } else if (bit) {
      output[out_byte] |= (1 << out_bit);
      out_index++;
    } else {
      out_index++;
      out_bit = 7 - (out_index % 8);
      if (out_bit == 7) {
        out_byte++;
      }
      output[out_byte] |= (1 << out_bit);
    }
    out_index++;
  }
}

void ProFlame2Component::transmit_command() {
  if (!this->spi_ready_) {
    ESP_LOGE(TAG, "SPI not ready, skipping transmission");
    return;
  }

  const uint32_t now = millis();
  if (now - this->last_transmission_ < MIN_TRANSMISSION_INTERVAL) {
    return;
  }

  // If currently transmitting, just mark pending (latest state wins)
  if (this->tx_state_ == TX_RUNNING || this->tx_repeat_left_ > 0) {
    this->tx_pending_ = true;
    return;
  }

  ESP_LOGD(TAG, "=== TRANSMIT DEBUG ===");
  ESP_LOGD(TAG,
           "State: Power=%d, Pilot=%s, Flame=%d, Fan=%d, Light=%d, Aux=%d, "
           "Secondary Flame=%d",
           this->current_state_.power,
           this->current_state_.pilot_cpi ? "CPI" : "IPI",
           this->current_state_.flame_level, this->current_state_.fan_level,
           this->current_state_.light_level, this->current_state_.aux_power,
           this->current_state_.secondary_flame);

  uint8_t packet[12];
  this->build_packet(packet);

  ESP_LOGD(TAG,
           "Raw packet (91 bits): %02X %02X %02X %02X %02X %02X %02X %02X %02X "
           "%02X %02X %02X",
           packet[0], packet[1], packet[2], packet[3], packet[4], packet[5],
           packet[6], packet[7], packet[8], packet[9], packet[10], packet[11]);

  uint8_t encoded[23];
  this->encode_manchester(packet, encoded, 91);

  ESP_LOGD(TAG, "Manchester encoded (182 bits = 23 bytes):");
  ESP_LOGD(TAG, "  %02X %02X %02X %02X %02X %02X %02X %02X", encoded[0],
           encoded[1], encoded[2], encoded[3], encoded[4], encoded[5],
           encoded[6], encoded[7]);
  ESP_LOGD(TAG, "  %02X %02X %02X %02X %02X %02X %02X %02X", encoded[8],
           encoded[9], encoded[10], encoded[11], encoded[12], encoded[13],
           encoded[14], encoded[15]);
  ESP_LOGD(TAG, "  %02X %02X %02X %02X %02X %02X %02X", encoded[16],
           encoded[17], encoded[18], encoded[19], encoded[20], encoded[21],
           encoded[22]);

  // Build one contiguous burst containing all repeats (rtl_433 sees one packet)
  const size_t single_len = 23;
  const size_t gap_len = 2;   // small zero gap (~6.7 ms at 2400 baud) between repeats
  const size_t repeats = TX_REPEAT_TARGET;
  const size_t total_len = repeats * (single_len + gap_len);
  if (total_len > sizeof(this->tx_buf_)) {
    ESP_LOGE(TAG, "TX buffer too small for repeats: need %u bytes", static_cast<unsigned>(total_len));
    return;
  }
  for (size_t r = 0; r < repeats; r++) {
    uint8_t *dst = this->tx_buf_ + r * (single_len + gap_len);
    memcpy(dst, encoded, single_len);
    memset(dst + single_len, 0x00, gap_len);  // zero gap bytes between repeats
  }
  this->tx_len_ = total_len;
  this->tx_repeat_left_ = 0;  // handled by single contiguous burst

  ESP_LOGD(TAG, "Sending single burst of %u bytes (%u repeats)", static_cast<unsigned>(this->tx_len_), static_cast<unsigned>(repeats));

  this->start_tx_(this->tx_buf_, this->tx_len_);
  this->last_transmission_ = now;
}

void ProFlame2Component::start_tx_(const uint8_t *data, size_t len) {
  if (len == 0 || len > sizeof(this->tx_buf_)) {
    ESP_LOGE(TAG, "TX buffer size invalid: %u", static_cast<unsigned>(len));
    this->tx_state_ = TX_ERROR;
    return;
  }
  ESP_LOGD(TAG, "TX start request: len=%u", static_cast<unsigned>(len));

  // (If called from repeat loop, tx_buf_ is already populated; but safe to
  // copy)
  memcpy(this->tx_buf_, data, len);
  this->tx_len_ = len;
  this->tx_pos_ = 0;
  this->tx_start_ms_ = millis();
  this->tx_state_ = TX_RUNNING;

  // Fixed packet length
  this->write_register(CC1101_PKTLEN, static_cast<uint8_t>(len));

  // Clean start
  this->send_strobe(CC1101_SIDLE);
  this->send_strobe(CC1101_SFTX);

  // Manual calibration before TX
  this->send_strobe(CC1101_SCAL);
#ifdef USE_ESP_IDF
  vTaskDelay(pdMS_TO_TICKS(5));
#else
  delay(5);
#endif

  // Prime FIFO (up to 64 bytes)
  const size_t first = std::min<size_t>(64, this->tx_len_);
  this->enable();
  this->write_byte(CC1101_TXFIFO_BURST);
  for (size_t i = 0; i < first; i++) {
    this->write_byte(this->tx_buf_[i]);
  }
  this->disable();
  this->tx_pos_ = first;

  // Start TX
  this->send_strobe(CC1101_STX);
#ifdef USE_ESP_IDF
  vTaskDelay(pdMS_TO_TICKS(2));
#else
  delay(2);
#endif

  uint8_t marc = this->read_status_register(CC1101_MARCSTATE) & 0x1F;
  uint8_t txb = this->read_status_register(CC1101_TXBYTES);
  ESP_LOGD(TAG, "TX started: MARCSTATE=0x%02X TXBYTES=0x%02X", marc, txb);
}

void ProFlame2Component::service_tx_() {
  if (this->tx_state_ != TX_RUNNING) {
    return;
  }

  const uint32_t now = millis();
  const uint8_t marc = this->read_status_register(CC1101_MARCSTATE) & 0x1F;
  const uint8_t txbytes_raw = this->read_status_register(CC1101_TXBYTES);
  const bool underflow = (txbytes_raw & 0x80) != 0;
  const uint8_t txbytes = txbytes_raw & 0x7F;

  if (underflow || marc == 0x16) {
    ESP_LOGE(TAG, "TX error: MARCSTATE=0x%02X TXBYTES=0x%02X (underflow=%d)",
             marc, txbytes_raw, underflow);
    this->send_strobe(CC1101_SIDLE);
    this->send_strobe(CC1101_SFTX);
    this->tx_state_ = TX_ERROR;
    this->tx_repeat_left_ = 0;
    return;
  }

  if (now - this->tx_start_ms_ > 2000) {
    ESP_LOGE(TAG, "TX timeout: MARCSTATE=0x%02X TXBYTES=%u pos=%u/%u", marc,
             txbytes, static_cast<unsigned>(this->tx_pos_),
             static_cast<unsigned>(this->tx_len_));
    this->send_strobe(CC1101_SIDLE);
    this->send_strobe(CC1101_SFTX);
    this->tx_state_ = TX_ERROR;
    this->tx_repeat_left_ = 0;
    return;
  }

  // KEEP THIS REFILL LOGIC (this is your snippet — it stays)
  if (this->tx_pos_ < this->tx_len_) {
    const size_t free = (txbytes >= 64) ? 0 : (64 - txbytes);
    if (free > 0) {
      const size_t remaining = this->tx_len_ - this->tx_pos_;
      const size_t chunk = std::min(free, remaining);

      this->enable();
      this->write_byte(CC1101_TXFIFO_BURST);  // TX FIFO burst write
      for (size_t i = 0; i < chunk; i++) {
        this->write_byte(this->tx_buf_[this->tx_pos_ + i]);
      }
      this->disable();
      this->tx_pos_ += chunk;

      ESP_LOGD(TAG,
               "TX refill: wrote=%u free=%u txbytes=%u pos=%u/%u marc=0x%02X",
               static_cast<unsigned>(chunk), static_cast<unsigned>(free),
               txbytes, static_cast<unsigned>(this->tx_pos_),
               static_cast<unsigned>(this->tx_len_), marc);
      YIELD();
    }
  }

  // Completion for fixed-length packet mode
  if (this->tx_pos_ >= this->tx_len_ && txbytes == 0 &&
      (marc == 0x01 || marc == 0x00)) {
    this->send_strobe(CC1101_SIDLE);
    this->send_strobe(CC1101_SFTX);
    this->tx_state_ = TX_IDLE;
    ESP_LOGD(TAG, "TX done: pos=%u len=%u", static_cast<unsigned>(this->tx_pos_),
             static_cast<unsigned>(this->tx_len_));

    // NOTE: tx_repeat_left_ is always 0 in normal operation; see transmit_command().
    // Count down repeats
    if (this->tx_repeat_left_ > 0) {
      this->tx_repeat_left_--;
    }

    if (this->tx_repeat_left_ > 0) {
      // schedule next repeat after a short gap
      this->tx_next_repeat_ms_ = millis() + TX_REPEAT_GAP_MS;
      ESP_LOGD(TAG, "TX repeat scheduled: left=%u gap=%ums",
               this->tx_repeat_left_, TX_REPEAT_GAP_MS);
      return;
    }

    ESP_LOGD(TAG, "TX complete (all repeats done)");

    // If something changed while we were blasting repeats, send latest state
    // now
    if (this->tx_pending_) {
      this->tx_pending_ = false;
      this->transmit_command();
    }
  }
}

void ProFlame2Component::queue_send() { this->send_pending_ = true; }

// Control methods
void ProFlame2Component::set_power(bool state) {
  if (this->current_state_.power != state) {
    this->current_state_.power = state;
    this->buffer_dirty_ = true;
    if (this->power_switch_) {
      this->power_switch_->publish_state(state);
    }
    ESP_LOGI(TAG, "Power set to %s", state ? "ON" : "OFF");
  }
}

void ProFlame2Component::set_pilot_mode(bool cpi_mode) {
  if (this->current_state_.pilot_cpi != cpi_mode) {
    this->current_state_.pilot_cpi = cpi_mode;
    this->buffer_dirty_ = true;
    if (this->pilot_switch_) {
      this->pilot_switch_->publish_state(cpi_mode);
    }
    ESP_LOGI(TAG, "Pilot mode set to %s", cpi_mode ? "CPI" : "IPI");
  }
}

void ProFlame2Component::set_flame_level(uint8_t level) {
  if (level > 6) {
    level = 6;
  }
  if (this->current_state_.flame_level != level) {
    this->current_state_.flame_level = level;
    this->buffer_dirty_ = true;
    if (this->flame_number_) {
      this->flame_number_->publish_state(level);
    }
    ESP_LOGI(TAG, "Flame level set to %d", level);
  }
}

void ProFlame2Component::set_fan_level(uint8_t level) {
  if (level > 6) {
    level = 6;
  }
  if (this->current_state_.fan_level != level) {
    this->current_state_.fan_level = level;
    this->buffer_dirty_ = true;
    if (this->fan_number_) {
      this->fan_number_->publish_state(level);
    }
    ESP_LOGI(TAG, "Fan level set to %d", level);
  }
}

void ProFlame2Component::set_light_level(uint8_t level) {
  if (level > 6) {
    level = 6;
  }
  if (this->current_state_.light_level != level) {
    this->current_state_.light_level = level;
    this->buffer_dirty_ = true;
    if (this->light_number_) {
      this->light_number_->publish_state(level);
    }
    ESP_LOGI(TAG, "Light level set to %d", level);
  }
}

void ProFlame2Component::set_aux_power(bool state) {
  if (this->current_state_.aux_power != state) {
    this->current_state_.aux_power = state;
    this->buffer_dirty_ = true;
    if (this->aux_switch_) {
      this->aux_switch_->publish_state(state);
    }
    ESP_LOGI(TAG, "Aux power set to %s", state ? "ON" : "OFF");
  }
}

void ProFlame2Component::set_secondary_flame(bool state) {
  if (this->current_state_.secondary_flame != state) {
    this->current_state_.secondary_flame = state;
    this->buffer_dirty_ = true;
    if (this->secondary_flame_switch_) {
      this->secondary_flame_switch_->publish_state(state);
    }
    ESP_LOGI(TAG, "Secondary flame set to %s", state ? "ON" : "OFF");
  }
}

void ProFlame2Component::set_thermostat(bool state) {
  if (this->current_state_.thermostat != state) {
    this->current_state_.thermostat = state;
    this->buffer_dirty_ = true;
    if (this->thermostat_switch_) {
      this->thermostat_switch_->publish_state(state);
    }
    ESP_LOGI(TAG, "Thermostat set to %s", state ? "ON" : "OFF");
  }
}

// DEBUG FUNCTIONS
void ProFlame2Component::debug_minimal_tx() {
  ESP_LOGI(TAG, "=== DEBUG: Sending minimal 23-byte test ===");
  uint8_t test_data[23];
  for (int i = 0; i < 23; i++) {
    test_data[i] = (i % 2) ? 0x55 : 0xAA;
  }

  memcpy(this->tx_buf_, test_data, 23);
  this->tx_len_ = 23;

  this->start_tx_(this->tx_buf_, this->tx_len_);
}

void ProFlame2Component::debug_check_config() {
  ESP_LOGI(TAG, "=== CC1101 Configuration Check ===");
  uint8_t freq0 = this->read_register(CC1101_FREQ0);
  uint8_t mdm4 = this->read_register(CC1101_MDMCFG4);
  uint8_t mdm3 = this->read_register(CC1101_MDMCFG3);
  uint8_t frend0 = this->read_register(CC1101_FREND0);
  uint8_t pktlen = this->read_register(CC1101_PKTLEN);

  ESP_LOGI(TAG, "FREQ0: 0x%02X (should be 0x89)", freq0);
  ESP_LOGI(TAG, "MDMCFG4: 0x%02X (should be 0xF6)", mdm4);
  ESP_LOGI(TAG, "MDMCFG3: 0x%02X (should be 0x83)", mdm3);
  ESP_LOGI(TAG, "FREND0: 0x%02X (should be 0x11)", frend0);
  ESP_LOGI(TAG, "PKTLEN: 0x%02X", pktlen);
}

}  // namespace proflame2
}  // namespace esphome
