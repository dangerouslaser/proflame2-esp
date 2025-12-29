#include "proflame2_cc1101.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#ifdef USE_ESP_IDF
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace esphome {
namespace proflame2 {

static const char *TAG = "proflame2";

// CC1101 Configuration for 314.973 MHz OOK at 2400 baud
// These values are calculated for 26MHz crystal
static const uint8_t CC1101_CONFIG[][2] = {
    // GDO0 used as a TX FIFO threshold indicator (assert when TX FIFO < threshold).
    // This helps us refill the FIFO in time without blocking.
    {CC1101_IOCFG0,   0x02},  // GDO0: TX FIFO below threshold / RX FIFO above threshold

    // TX FIFO threshold: choose a relatively high threshold so we get an early warning
    // (more time to refill before the FIFO empties).
    // 0x47 => TX threshold setting 7 (33 bytes).
    // FIFOTHR: keep RX threshold default-ish, set TX threshold to 33 bytes.
    // That gives ~110ms of airtime at 2400 baud before the FIFO empties.
    {CC1101_FIFOTHR,  0x47},  // RX=4, TX=7 (33B)
    // IMPORTANT: Use FIFO/packet mode (not async serial mode). In async serial mode the
    // CC1101 expects a serial data stream on a GDO pin and the TX FIFO writes will not
    // produce RF output. We want to push bytes into the TX FIFO.
    // PKTLEN will be set per-transmission to the exact frame length.
    {CC1101_PKTLEN,   125},
    {CC1101_PKTCTRL1, 0x00},  // No address check, no status append
    // Packet automation must be enabled (FIFO/packet mode), not async serial.
    // Use infinite length so we can stream >64B through the FIFO.
    // Use fixed-length packet mode so the radio stops cleanly at the end of the frame.
    // In infinite-length mode the CC1101 will eventually hit TXFIFO underflow unless you
    // abort transmission before the FIFO empties.
    {CC1101_PKTCTRL0, 0x00},  // LENGTH_CONFIG=0 (fixed), CRC off, whitening off
    {CC1101_FSCTRL1,  0x06},  // Frequency Synthesizer Control
    // Frequency word for 314.973 MHz (26 MHz crystal): 0x0C 0x1D 0x89
    {CC1101_FREQ2,    0x0C},
    {CC1101_FREQ1,    0x1D},
    {CC1101_FREQ0,    0x89},  // FIXED: Was 0x46, must be 0x89 for exact 314.973 MHz
    {CC1101_MDMCFG4,  0xF5},  // FIXED: 2400 baud (was 0x86 ~1.9kbps)
    {CC1101_MDMCFG3,  0x83},  // FIXED: 2400 baud (was 0x33)
    {CC1101_MDMCFG2,  0x30},  // Modem Configuration - OOK, no sync
    {CC1101_MDMCFG1,  0x00},  // Modem Configuration
    {CC1101_MDMCFG0,  0xF8},  // Modem Configuration
    {CC1101_DEVIATN,  0x00},  // Modem Deviation Setting (not used for OOK)

    // State machine
    // - Return to IDLE after TX
    // - Manual calibration for better VCO lock at 315MHz
    {CC1101_MCSM1,    0x00},  // FIXED: Was 0x30, cleaner TX/RX transitions
    {CC1101_MCSM0,    0x04},  // FIXED: Was 0x18, manual calibration only

    // Recommended front-end settings that improve ASK/OOK performance on many modules
    {CC1101_FREND1,   0x56},
    // ASK/OOK front-end.
    // IMPORTANT: PA_POWER=1 (low 3 bits) for proper OOK PA table usage
    {CC1101_FREND0,   0x11},  // FIXED: Was 0x10, must be 0x11 for OOK
    {CC1101_FSCAL3,   0xEA},  // FIXED: Was 0xE9, better for 315MHz
    {CC1101_FSCAL2,   0x2A},
    {CC1101_FSCAL1,   0x00},
    {CC1101_FSCAL0,   0x11},  // FIXED: Was 0x1F, better VCO lock
    {CC1101_TEST2,    0x81},
    {CC1101_TEST1,    0x35},
    {CC1101_TEST0,    0x09},
};

void ProFlame2Component::setup() {
    ESP_LOGCONFIG(TAG, "Setting up ProFlame 2 CC1101...");
    
    this->spi_setup();
    
    if (this->gdo0_pin_ != nullptr) {
        this->gdo0_pin_->setup();
        this->gdo0_pin_->pin_mode(gpio::FLAG_INPUT);
    }
    
    // Reset and configure CC1101
    this->reset_cc1101();
    // Use vTaskDelay for ESP-IDF to yield to scheduler, or delayMicroseconds for Arduino
    #ifdef USE_ESP_IDF
    vTaskDelay(pdMS_TO_TICKS(10));
    #else
    delay(10);
    #endif
    this->configure_cc1101();
    
    // CRITICAL: Set PA table for OOK (was missing!)
    // For OOK: index 0 = OFF power, index 1 = ON power
    uint8_t pa_table[8] = {
        0x00,  // Index 0: OFF power (0 dBm)
        0xC0,  // Index 1: ON power (+10 dBm maximum)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    ESP_LOGD(TAG, "Setting PA table for OOK");
    this->enable();
    this->write_byte(CC1101_PATABLE | 0x40);  // Burst write to PA table
    for (int i = 0; i < 8; i++) {
        this->write_byte(pa_table[i]);
    }
    this->disable();
    
    // Verify PA table was written correctly
    uint8_t pa_verify[2];
    this->enable();
    this->write_byte(CC1101_PATABLE | 0xC0);  // Burst read
    pa_verify[0] = this->read_byte();
    pa_verify[1] = this->read_byte();
    this->disable();
    ESP_LOGD(TAG, "PA Table verified: OFF=0x%02X, ON=0x%02X", pa_verify[0], pa_verify[1]);
    
    // Initialize state
    this->current_state_ = ProFlame2Command{
        .pilot_cpi = false,
        .light_level = 0,
        .thermostat = false,
        .power = false,
        .secondary_flame = false,
        .fan_level = 0,
        .aux_power = false,
        .flame_level = 0
    };
    
    ESP_LOGCONFIG(TAG, "ProFlame 2 setup complete");
}

void ProFlame2Component::loop() {
    this->service_tx_();
}

void ProFlame2Component::dump_config() {
    ESP_LOGCONFIG(TAG, "ProFlame 2 CC1101:");
    ESP_LOGCONFIG(TAG, "  Serial Number: 0x%08X", this->serial_number_);
    if (this->gdo0_pin_ != nullptr) {
        LOG_PIN("  GDO0 Pin: ", this->gdo0_pin_);
    }

    // Basic SPI sanity: these are CC1101 status registers.
    const uint8_t partnum = this->read_status_register(0x30);
    const uint8_t version = this->read_status_register(0x31);
    ESP_LOGCONFIG(TAG, "  CC1101 Part Number: 0x%02X", partnum);
    ESP_LOGCONFIG(TAG, "  CC1101 Version: 0x%02X", version);

    // Log key RF settings we're relying on for RTL/SDR bring-up.
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
    this->write_byte(reg | 0x80);  // Read flag
    uint8_t value = this->read_byte();
    this->disable();
    return value;
}

uint8_t ProFlame2Component::read_status_register(uint8_t reg) {
    // Status registers must be read with burst + read bits set (0xC0)
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
    // CC1101 reset sequence (simplified):
    // 1) CSn high -> low -> high -> low with timing gaps
    // 2) SRES strobe while CSn is asserted
    // NOTE: The "proper" sequence includes waiting for SO(MISO) to go low.
    // In ESPHome we don't have direct access to SO level here, so we keep
    // timings generous and rely on subsequent register reads for sanity.

    this->disable();
    delayMicroseconds(5);

    this->enable();
    delayMicroseconds(10);
    this->disable();
    delayMicroseconds(40);

    this->enable();
    this->write_byte(CC1101_SRES);
    this->disable();
    // Use vTaskDelay for ESP-IDF to yield to scheduler
    #ifdef USE_ESP_IDF
    vTaskDelay(pdMS_TO_TICKS(2));
    #else
    delay(2);
    #endif
}

void ProFlame2Component::configure_cc1101() {
    // Write configuration registers
    for (size_t i = 0; i < sizeof(CC1101_CONFIG) / sizeof(CC1101_CONFIG[0]); i++) {
        this->write_register(CC1101_CONFIG[i][0], CC1101_CONFIG[i][1]);
    }
    
    // Set to IDLE state
    this->send_strobe(CC1101_SIDLE);
    
    // Flush FIFOs
    this->send_strobe(0x3A);  // SFTX
    this->send_strobe(0x3B);  // SFRX

    // Set PA table (output power).
    // For ASK/OOK, the CC1101 uses two PA table entries: one for '0' and one for '1'.
    // Many modules also behave better if you populate the *whole* PA table so the
    // active index can't accidentally land on an all-zeros entry.
    //
    // We'll use:
    //   entry0..7 = 0xC0 (high power; common on CC1101 modules). In OOK mode the CC1101
    // toggles the PA on/off internally; the PA table entry selects the on-level power.
    this->enable();
    this->write_byte(CC1101_PATABLE | 0x40);  // burst write
    for (int i = 0; i < 8; i++) {
        this->write_byte(0xC0);  // TX power level for OOK
    }
    this->disable();

    // Force a calibration once at boot so we start from a known-good state.
    this->send_strobe(CC1101_SCAL);
    
    ESP_LOGD(TAG, "CC1101 configured for 314.973 MHz OOK at 2400 baud");
}

uint8_t ProFlame2Component::calculate_parity(uint16_t data) {
    // Count number of 1s in the data (including padding bit)
    uint8_t ones = 0;
    for (int i = 0; i < 9; i++) {
        if (data & (1 << i)) ones++;
    }
    // Return 1 if odd number of ones, 0 if even
    return ones & 1;
}

uint8_t ProFlame2Component::calculate_checksum(uint8_t cmd_byte, uint8_t c_const, uint8_t d_const) {
    uint8_t high_nibble = (cmd_byte >> 4) & 0x0F;
    uint8_t low_nibble = cmd_byte & 0x0F;
    
    uint8_t x = (c_const ^ (high_nibble << 1) ^ high_nibble ^ (low_nibble << 1)) & 0x0F;
    uint8_t y = (d_const ^ high_nibble ^ low_nibble) & 0x0F;
    
    return (x << 4) | y;
}

void ProFlame2Component::build_packet(uint8_t *packet) {
    // Clear packet buffer (91 bits = 12 bytes)
    memset(packet, 0, 12);
    
    // Extract serial number words (assuming serial is 24 bits)
    uint16_t serial1 = (this->serial_number_ >> 16) & 0xFF;
    uint16_t serial2 = (this->serial_number_ >> 8) & 0xFF;
    uint16_t serial3 = this->serial_number_ & 0xFF;
    
    // Build command bytes
    uint8_t cmd1 = (this->current_state_.pilot_cpi ? 0x80 : 0x00) |
                   ((this->current_state_.light_level & 0x07) << 4) |
                   (this->current_state_.thermostat ? 0x02 : 0x00) |
                   (this->current_state_.power ? 0x01 : 0x00);
    
    uint8_t cmd2 = (this->current_state_.secondary_flame ? 0x80 : 0x00) |
                   ((this->current_state_.fan_level & 0x07) << 4) |
                   (this->current_state_.aux_power ? 0x08 : 0x00) |
                   (this->current_state_.flame_level & 0x07);
    
    // Calculate checksums
    uint8_t checksum1 = this->calculate_checksum(cmd1, 0x0D, 0x00);  // C=0b1101, D=0
    uint8_t checksum2 = this->calculate_checksum(cmd2, 0x00, 0x07);  // C=0, D=0b0111
    
    // Build 7 words of 13 bits each
    // Word format: S(1) | Guard(1) | Data(8) | Padding(1) | Parity(1) | Guard(1)
    // S is handled in Manchester encoding as sync pattern
    
    uint16_t words[7];
    
    // Serial words (first word has padding bit = 1, others = 0)
    words[0] = 0x1000 | (serial1 << 3) | 0x200 | (calculate_parity(serial1 | 0x100) << 1) | 0x01;
    words[1] = 0x1000 | (serial2 << 3) | (calculate_parity(serial2) << 1) | 0x01;
    words[2] = 0x1000 | (serial3 << 3) | (calculate_parity(serial3) << 1) | 0x01;
    
    // Command words
    words[3] = 0x1000 | (cmd1 << 3) | (calculate_parity(cmd1) << 1) | 0x01;
    words[4] = 0x1000 | (cmd2 << 3) | (calculate_parity(cmd2) << 1) | 0x01;
    
    // Checksum words
    words[5] = 0x1000 | (checksum1 << 3) | (calculate_parity(checksum1) << 1) | 0x01;
    words[6] = 0x1000 | (checksum2 << 3) | (calculate_parity(checksum2) << 1) | 0x01;
    
    // Pack words into bit array (91 bits total)
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

void ProFlame2Component::encode_manchester(uint8_t *input, uint8_t *output, size_t input_bits) {
    // Thomas Manchester encoding:
    // 0 -> 01
    // 1 -> 10
    // Sync (S) -> 11
    // Padding (Z) -> 00
    
    int out_index = 0;
    memset(output, 0, 46);  // 182 bits = 23 bytes
    
    for (size_t i = 0; i < input_bits; i++) {
        int byte_index = i / 8;
        int bit_offset = 7 - (i % 8);
        bool bit = (input[byte_index] >> bit_offset) & 1;
        
        // Check if this is a sync position (every 13 bits, at position 0)
        bool is_sync = (i % 13) == 0;
        
        int out_byte = out_index / 8;
        int out_bit = 7 - (out_index % 8);
        
        if (is_sync) {
            // Sync pattern: 11
            output[out_byte] |= (1 << out_bit);
            out_index++;
            out_bit = 7 - (out_index % 8);
            if (out_bit == 7) out_byte++;
            output[out_byte] |= (1 << out_bit);
        } else if (bit) {
            // 1 -> 10
            output[out_byte] |= (1 << out_bit);
            out_index++;
            // Second bit is 0, already cleared
        } else {
            // 0 -> 01
            // First bit is 0, already cleared
            out_index++;
            out_bit = 7 - (out_index % 8);
            if (out_bit == 7) out_byte++;
            output[out_byte] |= (1 << out_bit);
        }
        out_index++;
    }
}

void ProFlame2Component::transmit_command() {
    // Rate limiting
    uint32_t now = millis();
    if (now - this->last_transmission_ < MIN_TRANSMISSION_INTERVAL) {
        return;
    }

    // If a TX is currently running, just mark this as pending. We'll send the latest
    // state once the current TX completes.
    if (this->tx_state_ == TX_RUNNING) {
        this->tx_pending_ = true;
        return;
    }
    
    ESP_LOGI(TAG, "=== TRANSMIT DEBUG ===");
    ESP_LOGI(TAG, "State: Power=%d, Pilot=%s, Flame=%d, Fan=%d, Light=%d, Aux=%d, Secondary Flame=%d", 
             this->current_state_.power, 
             this->current_state_.pilot_cpi ? "CPI" : "IPI",
             this->current_state_.flame_level,
             this->current_state_.fan_level,
             this->current_state_.light_level,
             this->current_state_.aux_power,
             this->current_state_.secondary_flame);
    
    // Build packet
    uint8_t packet[12];  // 91 bits
    memset(packet, 0, 12);
    this->build_packet(packet);
    
    // Debug: show raw packet
    ESP_LOGI(TAG, "Raw packet (91 bits): %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
             packet[0], packet[1], packet[2], packet[3], packet[4], packet[5],
             packet[6], packet[7], packet[8], packet[9], packet[10], packet[11]);
    
    // Encode with Manchester encoding
    uint8_t encoded[23];  // 182 bits = 23 bytes (CRITICAL: was 46!)
    memset(encoded, 0, 23);
    this->encode_manchester(packet, encoded, 91);
    
    // Debug: show encoded packet
    ESP_LOGI(TAG, "Manchester encoded (182 bits = 23 bytes):");
    ESP_LOGI(TAG, "  %02X %02X %02X %02X %02X %02X %02X %02X",
             encoded[0], encoded[1], encoded[2], encoded[3], 
             encoded[4], encoded[5], encoded[6], encoded[7]);
    ESP_LOGI(TAG, "  %02X %02X %02X %02X %02X %02X %02X %02X",
             encoded[8], encoded[9], encoded[10], encoded[11],
             encoded[12], encoded[13], encoded[14], encoded[15]);
    ESP_LOGI(TAG, "  %02X %02X %02X %02X %02X %02X %02X",
             encoded[16], encoded[17], encoded[18], encoded[19],
             encoded[20], encoded[21], encoded[22]);
    
    // CRITICAL FIX: Send ONLY the 23-byte encoded packet
    // NOT 5 repetitions of 125 bytes!
    ESP_LOGI(TAG, "Sending SINGLE packet: 23 bytes = 184 bits");
    ESP_LOGI(TAG, "This should show as {184} in rtl_433, NOT {1000}!");
    
    // Make sure we're in a known state and TX FIFO is clean.
    this->send_strobe(CC1101_SIDLE);
    this->send_strobe(0x3A);  // SFTX

    // Send ONLY the encoded packet (23 bytes), not repetitions!
    this->start_tx_(encoded, 23);  // CRITICAL: Only 23 bytes!
    this->last_transmission_ = now;
}

void ProFlame2Component::start_tx_(const uint8_t *data, size_t len) {
    if (len == 0 || len > sizeof(this->tx_buf_)) {
        ESP_LOGE(TAG, "TX buffer size invalid: %u", static_cast<unsigned>(len));
        return;
    }

    memcpy(this->tx_buf_, data, len);
    this->tx_len_ = len;
    this->tx_pos_ = 0;
    this->tx_start_ms_ = millis();
    this->tx_state_ = TX_RUNNING;
    this->tx_pending_ = false;

    // Program fixed packet length so the CC1101 terminates TX cleanly when done.
    // (Avoids TX FIFO underflow at the end of an infinite-length stream.)
    if (len > 255) {
        ESP_LOGE(TAG, "TX length too large for fixed packet mode: %u", static_cast<unsigned>(len));
        this->tx_state_ = TX_ERROR;
        return;
    }
    this->write_register(CC1101_PKTLEN, static_cast<uint8_t>(len));

    // Known-good state and clean FIFO
    this->send_strobe(CC1101_SIDLE);
    this->send_strobe(0x3A);  // SFTX
    
    // CRITICAL: Manual calibration before TX (fixes VCO lock issues at 315MHz)
    ESP_LOGD(TAG, "Performing manual calibration before TX");
    this->send_strobe(CC1101_SCAL);
    // Use vTaskDelay for ESP-IDF to yield to scheduler
    #ifdef USE_ESP_IDF
    vTaskDelay(pdMS_TO_TICKS(5));
    #else
    delay(5);
    #endif
    
    // Verify we're in IDLE after calibration
    uint8_t marc_cal = this->read_status_register(CC1101_MARCSTATE) & 0x1F;
    if (marc_cal != 0x01) {
        ESP_LOGW(TAG, "Calibration may have failed, MARCSTATE=0x%02X", marc_cal);
        // Try to force IDLE
        this->send_strobe(CC1101_SIDLE);
        #ifdef USE_ESP_IDF
        vTaskDelay(pdMS_TO_TICKS(1));
        #else
        delay(1);
        #endif
    }

    // Prime FIFO as full as we reasonably can. The CC1101 TX FIFO is 64 bytes.
    // Priming deep reduces the chance of an early underflow if the main loop is
    // busy for a short burst.
    const size_t first = std::min<size_t>(64, this->tx_len_);
    this->enable();
    this->write_byte(0x7F);  // TX FIFO burst write
    for (size_t i = 0; i < first; i++) {
        this->write_byte(this->tx_buf_[i]);
    }
    this->disable();
    this->tx_pos_ = first;

    uint8_t st = this->read_status_register(CC1101_MARCSTATE) & 0x1F;
    ESP_LOGD(TAG, "TX start: primed=%u bytes, MARCSTATE=0x%02X", static_cast<unsigned>(first), st);

    // Start TX
    this->send_strobe(CC1101_STX);
}

void ProFlame2Component::service_tx_() {
    if (this->tx_state_ != TX_RUNNING) {
        return;
    }

    const uint32_t now = millis();

    // Read status with correct access method.
    const uint8_t marc = this->read_status_register(CC1101_MARCSTATE) & 0x1F;
    const uint8_t txbytes_raw = this->read_status_register(CC1101_TXBYTES);
    const bool underflow = (txbytes_raw & 0x80) != 0;
    const uint8_t txbytes = txbytes_raw & 0x7F;

    // TX FIFO underflow or hung radio -> recover.
    if (underflow || marc == 0x16) {
        ESP_LOGE(TAG, "TX error: MARCSTATE=0x%02X TXBYTES=0x%02X (underflow=%d)", marc, txbytes_raw, underflow);
        this->send_strobe(CC1101_SIDLE);
        this->send_strobe(0x3A);  // SFTX
        this->tx_state_ = TX_ERROR;
        return;
    }

    // Timeout guard (should be << 2s at 2400 baud for our current frame size)
    if (now - this->tx_start_ms_ > 2000) {
        ESP_LOGE(TAG, "TX timeout: MARCSTATE=0x%02X TXBYTES=%u pos=%u/%u", marc, txbytes,
                 static_cast<unsigned>(this->tx_pos_), static_cast<unsigned>(this->tx_len_));
        this->send_strobe(CC1101_SIDLE);
        this->send_strobe(0x3A);  // SFTX
        this->tx_state_ = TX_ERROR;
        return;
    }

    // Refill FIFO while TX is running.
    // FIFO is 64 bytes. Write whenever there is *any* free space; tiny top-ups are
    // preferable to a TX underflow (which hard-stops the radio).
    if (this->tx_pos_ < this->tx_len_) {
        const size_t free = (txbytes >= 64) ? 0 : (64 - txbytes);
        if (free > 0) {
            const size_t remaining = this->tx_len_ - this->tx_pos_;
            const size_t chunk = std::min(free, remaining);

            this->enable();
            this->write_byte(0x7F);  // TX FIFO burst write
            for (size_t i = 0; i < chunk; i++) {
                this->write_byte(this->tx_buf_[this->tx_pos_ + i]);
            }
            this->disable();
            this->tx_pos_ += chunk;

            ESP_LOGD(TAG, "TX refill: wrote=%u free=%u txbytes=%u pos=%u/%u marc=0x%02X",
                     static_cast<unsigned>(chunk), static_cast<unsigned>(free), txbytes,
                     static_cast<unsigned>(this->tx_pos_), static_cast<unsigned>(this->tx_len_), marc);
        }
    }

    // Completion: in fixed-length mode the CC1101 returns to IDLE automatically when
    // PKTLEN bytes have been clocked out. We treat "done" as: all bytes queued by us,
    // TX FIFO drained, and MARCSTATE back in IDLE.
    if (this->tx_pos_ >= this->tx_len_ && txbytes == 0 && (marc == 0x01 || marc == 0x00)) {
        this->send_strobe(CC1101_SIDLE);
        this->send_strobe(0x3A);  // SFTX (clear any residual flags)
        this->tx_state_ = TX_IDLE;
        ESP_LOGD(TAG, "TX complete");

        if (this->tx_pending_) {
            // A newer state was requested while we were transmitting.
            this->transmit_command();
        }
    }
}

// Control method implementations
void ProFlame2Component::set_power(bool state) {
    if (this->current_state_.power != state) {
        this->current_state_.power = state;
        this->transmit_command();
        if (this->power_switch_) {
            this->power_switch_->publish_state(state);
        }
        ESP_LOGI(TAG, "Power set to %s", state ? "ON" : "OFF");
    }
}

void ProFlame2Component::set_pilot_mode(bool cpi_mode) {
    if (this->current_state_.pilot_cpi != cpi_mode) {
        this->current_state_.pilot_cpi = cpi_mode;
        this->transmit_command();
        if (this->pilot_switch_) {
            this->pilot_switch_->publish_state(cpi_mode);
        }
        ESP_LOGI(TAG, "Pilot mode set to %s", cpi_mode ? "CPI" : "IPI");
    }
}

void ProFlame2Component::set_flame_level(uint8_t level) {
    if (level > 6) level = 6;
    if (this->current_state_.flame_level != level) {
        this->current_state_.flame_level = level;
        this->transmit_command();
        if (this->flame_number_) {
            this->flame_number_->publish_state(level);
        }
        ESP_LOGI(TAG, "Flame level set to %d", level);
    }
}

void ProFlame2Component::set_fan_level(uint8_t level) {
    if (level > 6) level = 6;
    if (this->current_state_.fan_level != level) {
        this->current_state_.fan_level = level;
        this->transmit_command();
        if (this->fan_number_) {
            this->fan_number_->publish_state(level);
        }
        ESP_LOGI(TAG, "Fan level set to %d", level);
    }
}

void ProFlame2Component::set_light_level(uint8_t level) {
    if (level > 6) level = 6;
    if (this->current_state_.light_level != level) {
        this->current_state_.light_level = level;
        this->transmit_command();
        if (this->light_number_) {
            this->light_number_->publish_state(level);
        }
        ESP_LOGI(TAG, "Light level set to %d", level);
    }
}

void ProFlame2Component::set_aux_power(bool state) {
    if (this->current_state_.aux_power != state) {
        this->current_state_.aux_power = state;
        this->transmit_command();
        if (this->aux_switch_) {
            this->aux_switch_->publish_state(state);
        }
        ESP_LOGI(TAG, "Aux power set to %s", state ? "ON" : "OFF");
    }
}

void ProFlame2Component::set_secondary_flame(bool state) {
    if (this->current_state_.secondary_flame != state) {
        this->current_state_.secondary_flame = state;
        this->transmit_command();
        if (this->secondary_flame_switch_) {
            this->secondary_flame_switch_->publish_state(state);
        }
        ESP_LOGI(TAG, "Secondary flame set to %s", state ? "ON" : "OFF");
    }
}

void ProFlame2Component::set_thermostat(bool state) {
    if (this->current_state_.thermostat != state) {
        this->current_state_.thermostat = state;
        this->transmit_command();
        if (this->thermostat_switch_) {
            this->thermostat_switch_->publish_state(state);
        }
        ESP_LOGI(TAG, "Thermostat set to %s", state ? "ON" : "OFF");
    }
}

// DEBUG FUNCTIONS
void ProFlame2Component::debug_minimal_tx() {
    ESP_LOGI(TAG, "=== DEBUG: Sending minimal 23-byte test ===");
    
    // Create exactly 23 bytes of test data
    uint8_t test_data[23];
    
    // Fill with alternating pattern that's easy to spot
    for (int i = 0; i < 23; i++) {
        test_data[i] = (i % 2) ? 0x55 : 0xAA;
    }
    
    ESP_LOGI(TAG, "Test pattern (23 bytes):");
    for (int i = 0; i < 23; i++) {
        ESP_LOGD(TAG, "  [%02d]: 0x%02X", i, test_data[i]);
    }
    
    // Send EXACTLY 23 bytes
    this->send_strobe(CC1101_SIDLE);
    this->send_strobe(0x3A);  // SFTX
    
    // CRITICAL: Set packet length to 23!
    this->write_register(CC1101_PKTLEN, 23);
    ESP_LOGI(TAG, "Set PKTLEN to 23 bytes");
    
    // Send using the non-blocking TX
    this->start_tx_(test_data, 23);
    
    ESP_LOGI(TAG, "Sent 23 bytes = 184 bits");
    ESP_LOGI(TAG, "Check rtl_433: should show {184} bits, NOT {1000}!");
}

void ProFlame2Component::debug_check_config() {
    ESP_LOGI(TAG, "=== CC1101 Configuration Check ===");
    
    // Check critical registers
    uint8_t freq0 = this->read_register(CC1101_FREQ0);
    uint8_t mdm4 = this->read_register(CC1101_MDMCFG4);
    uint8_t mdm3 = this->read_register(CC1101_MDMCFG3);
    uint8_t frend0 = this->read_register(CC1101_FREND0);
    uint8_t pktlen = this->read_register(CC1101_PKTLEN);
    
    ESP_LOGI(TAG, "FREQ0: 0x%02X (should be 0x89)", freq0);
    ESP_LOGI(TAG, "MDMCFG4: 0x%02X (should be 0xF5 for 2400 baud)", mdm4);
    ESP_LOGI(TAG, "MDMCFG3: 0x%02X (should be 0x83 for 2400 baud)", mdm3);
    ESP_LOGI(TAG, "FREND0: 0x%02X (should be 0x11)", frend0);
    ESP_LOGI(TAG, "PKTLEN: 0x%02X", pktlen);
    
    // Check PA table
    uint8_t pa_table[2];
    this->enable();
    this->write_byte(CC1101_PATABLE | 0xC0);
    pa_table[0] = this->read_byte();
    pa_table[1] = this->read_byte();
    this->disable();
    ESP_LOGI(TAG, "PA Table: OFF=0x%02X, ON=0x%02X", pa_table[0], pa_table[1]);
    
    // Calculate actual frequency
    uint32_t freq_reg = (this->read_register(CC1101_FREQ2) << 16) |
                       (this->read_register(CC1101_FREQ1) << 8) |
                       freq0;
    float frequency = (freq_reg * 26.0f) / 65536.0f;
    ESP_LOGI(TAG, "Actual frequency: %.3f MHz", frequency);
    
    if (freq0 != 0x89) {
        ESP_LOGE(TAG, "CRITICAL: Frequency is wrong! Fix FREQ0 to 0x89");
    }
    if (mdm4 != 0xF5 || mdm3 != 0x83) {
        ESP_LOGE(TAG, "CRITICAL: Data rate is wrong! Must be 2400 baud");
    }
}

}  // namespace proflame2
}  // namespace esphome
