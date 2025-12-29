#pragma once

#include "esphome.h"
#include "esphome/core/component.h"
#include "esphome/components/spi/spi.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/fan/fan.h"
#include "esphome/components/number/number.h"

namespace esphome {
namespace proflame2 {

// CC1101 Register definitions
static const uint8_t CC1101_IOCFG2    = 0x00;
static const uint8_t CC1101_IOCFG1    = 0x01;
static const uint8_t CC1101_IOCFG0    = 0x02;
static const uint8_t CC1101_FIFOTHR   = 0x03;
static const uint8_t CC1101_SYNC1     = 0x04;
static const uint8_t CC1101_SYNC0     = 0x05;
static const uint8_t CC1101_PKTLEN    = 0x06;
static const uint8_t CC1101_PKTCTRL1  = 0x07;
static const uint8_t CC1101_PKTCTRL0  = 0x08;
static const uint8_t CC1101_ADDR      = 0x09;
static const uint8_t CC1101_CHANNR    = 0x0A;
static const uint8_t CC1101_FSCTRL1   = 0x0B;
static const uint8_t CC1101_FSCTRL0   = 0x0C;
static const uint8_t CC1101_FREQ2     = 0x0D;
static const uint8_t CC1101_FREQ1     = 0x0E;
static const uint8_t CC1101_FREQ0     = 0x0F;
static const uint8_t CC1101_MDMCFG4   = 0x10;
static const uint8_t CC1101_MDMCFG3   = 0x11;
static const uint8_t CC1101_MDMCFG2   = 0x12;
static const uint8_t CC1101_MDMCFG1   = 0x13;
static const uint8_t CC1101_MDMCFG0   = 0x14;
static const uint8_t CC1101_DEVIATN   = 0x15;

// State machine / calibration / analog front-end
static const uint8_t CC1101_MCSM2     = 0x16;
static const uint8_t CC1101_MCSM1     = 0x17;
static const uint8_t CC1101_MCSM0     = 0x18;
static const uint8_t CC1101_FOCCFG    = 0x19;
static const uint8_t CC1101_BSCFG     = 0x1A;
static const uint8_t CC1101_AGCCTRL2  = 0x1B;
static const uint8_t CC1101_AGCCTRL1  = 0x1C;
static const uint8_t CC1101_AGCCTRL0  = 0x1D;
static const uint8_t CC1101_FREND1    = 0x21;
static const uint8_t CC1101_FREND0    = 0x22;
static const uint8_t CC1101_FSCAL3    = 0x23;
static const uint8_t CC1101_FSCAL2    = 0x24;
static const uint8_t CC1101_FSCAL1    = 0x25;
static const uint8_t CC1101_FSCAL0    = 0x26;
static const uint8_t CC1101_TEST2     = 0x2C;
static const uint8_t CC1101_TEST1     = 0x2D;
static const uint8_t CC1101_TEST0     = 0x2E;

// Additional registers needed for TX state management
static const uint8_t CC1101_MARCSTATE = 0x35;  // Status register (read with 0xC0)
static const uint8_t CC1101_TXBYTES   = 0x3A;  // Status register (read with 0xC0)
static const uint8_t CC1101_PATABLE   = 0x3E;  // PA table

// CC1101 Strobe commands
static const uint8_t CC1101_SRES    = 0x30;
static const uint8_t CC1101_SFSTXON = 0x31;
static const uint8_t CC1101_SXOFF   = 0x32;
static const uint8_t CC1101_SCAL    = 0x33;
static const uint8_t CC1101_SRX     = 0x34;
static const uint8_t CC1101_STX     = 0x35;
static const uint8_t CC1101_SIDLE   = 0x36;

// ProFlame 2 packet structure
struct ProFlame2Command {
    // Command Word 1: CPI | Light[3] | 00 | Thermostat | Power
    bool pilot_cpi;      // 0=IPI, 1=CPI
    uint8_t light_level; // 0-6
    bool thermostat;
    bool power;
    
    // Command Word 2: Secondary Flame | Fan[3] | Aux | Flame[3]
    bool secondary_flame;
    uint8_t fan_level;   // 0-6
    bool aux_power;
    uint8_t flame_level; // 0-6
};

class ProFlame2Component : public Component, 
                          public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, 
                                               spi::CLOCK_POLARITY_LOW,
                                               spi::CLOCK_PHASE_LEADING,
                                               // Start conservative; CC1101 is happy faster, but 1MHz helps
                                               // eliminate signal-integrity issues during bring-up.
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
    
    // Control methods
    void set_power(bool state);
    void set_pilot_mode(bool cpi_mode);
    void set_flame_level(uint8_t level);
    void set_fan_level(uint8_t level);
    void set_light_level(uint8_t level);
    void set_aux_power(bool state);
    void set_secondary_flame(bool state);
    void set_thermostat(bool state);
    
    // Switch components
    void set_power_switch(switch_::Switch *sw) { this->power_switch_ = sw; }
    void set_pilot_switch(switch_::Switch *sw) { this->pilot_switch_ = sw; }
    void set_aux_switch(switch_::Switch *sw) { this->aux_switch_ = sw; }
    void set_secondary_flame_switch(switch_::Switch *sw) { this->secondary_flame_switch_ = sw; }
    void set_thermostat_switch(switch_::Switch *sw) { this->thermostat_switch_ = sw; }
    
    // Number components for levels
    void set_flame_number(number::Number *num) { this->flame_number_ = num; }
    void set_fan_number(number::Number *num) { this->fan_number_ = num; }
    void set_light_number(number::Number *num) { this->light_number_ = num; }
    
    // DEBUG FUNCTIONS (public for testing)
    void debug_minimal_tx();
    void debug_check_config();
    

    // moved from protected for debugging in yaml
    void send_strobe(uint8_t strobe);
    void write_register(uint8_t reg, uint8_t value);
    uint8_t read_status_register(uint8_t reg);
    uint8_t read_register(uint8_t reg);
   
    ProFlame2Command current_state_{};
    void transmit_command();
    void build_packet(uint8_t *packet);
    void encode_manchester(uint8_t *input, uint8_t *output, size_t input_len);

 protected:
    // CC1101 communication methods
    // void write_register(uint8_t reg, uint8_t value);
    // uint8_t read_register(uint8_t reg);
    // uint8_t read_status_register(uint8_t reg);
    // void send_strobe(uint8_t strobe);
    void reset_cc1101();
    void configure_cc1101();
    
    // ProFlame 2 protocol methods
    // void build_packet(uint8_t *packet);
    // void encode_manchester(uint8_t *input, uint8_t *output, size_t input_len);
    uint8_t calculate_checksum(uint8_t cmd_byte, uint8_t c_const, uint8_t d_const);
    uint8_t calculate_parity(uint16_t data);
    // void transmit_command();

    // Non-blocking TX state machine
    void start_tx_(const uint8_t *data, size_t len);
    void service_tx_();
    
    // Hardware pins
    GPIOPin *gdo0_pin_{nullptr};
    
    // Configuration
    uint32_t serial_number_{0x12345678};  // Default serial, should be configured
    
    // Current state

    // ProFlame2Command current_state_{};
    
    // Component references
    switch_::Switch *power_switch_{nullptr};
    switch_::Switch *pilot_switch_{nullptr};
    switch_::Switch *aux_switch_{nullptr};
    switch_::Switch *secondary_flame_switch_{nullptr};
    switch_::Switch *thermostat_switch_{nullptr};
    
    number::Number *flame_number_{nullptr};
    number::Number *fan_number_{nullptr};
    number::Number *light_number_{nullptr};
    
    // Timing
    uint32_t last_transmission_{0};
    static const uint32_t MIN_TRANSMISSION_INTERVAL = 200;  // ms between transmissions

    // TX state
    enum TxState : uint8_t { TX_IDLE = 0, TX_RUNNING = 1, TX_ERROR = 2 };
    TxState tx_state_{TX_IDLE};
    uint8_t tx_buf_[160]{};     // enough for our current frame
    size_t tx_len_{0};
    size_t tx_pos_{0};
    uint32_t tx_start_ms_{0};
    bool tx_pending_{false};
};

// Switch implementations
class ProFlame2PowerSwitch : public switch_::Switch, public Component {
 public:
    void set_parent(ProFlame2Component *parent) { this->parent_ = parent; }
    void write_state(bool state) override {
        this->parent_->set_power(state);
        this->publish_state(state);
    }
 protected:
    ProFlame2Component *parent_;
};

class ProFlame2PilotSwitch : public switch_::Switch, public Component {
 public:
    void set_parent(ProFlame2Component *parent) { this->parent_ = parent; }
    void write_state(bool state) override {
        this->parent_->set_pilot_mode(state);
        this->publish_state(state);
    }
 protected:
    ProFlame2Component *parent_;
};

class ProFlame2AuxSwitch : public switch_::Switch, public Component {
 public:
    void set_parent(ProFlame2Component *parent) { this->parent_ = parent; }
    void write_state(bool state) override {
        this->parent_->set_aux_power(state);
        this->publish_state(state);
    }
 protected:
    ProFlame2Component *parent_;
};

// Number component implementations
class ProFlame2FlameNumber : public number::Number, public Component {
 public:
    void set_parent(ProFlame2Component *parent) { this->parent_ = parent; }
    void control(float value) override {
        uint8_t level = static_cast<uint8_t>(value);
        this->parent_->set_flame_level(level);
        this->publish_state(level);
    }
 protected:
    ProFlame2Component *parent_;
};

class ProFlame2FanNumber : public number::Number, public Component {
 public:
    void set_parent(ProFlame2Component *parent) { this->parent_ = parent; }
    void control(float value) override {
        uint8_t level = static_cast<uint8_t>(value);
        this->parent_->set_fan_level(level);
        this->publish_state(level);
    }
 protected:
    ProFlame2Component *parent_;
};

class ProFlame2LightNumber : public number::Number, public Component {
 public:
    void set_parent(ProFlame2Component *parent) { this->parent_ = parent; }
    void control(float value) override {
        uint8_t level = static_cast<uint8_t>(value);
        this->parent_->set_light_level(level);
        this->publish_state(level);
    }
 protected:
    ProFlame2Component *parent_;
};

class ProFlame2SecondaryFlameSwitch : public switch_::Switch, public Component {
 public:
    void set_parent(ProFlame2Component *parent) { this->parent_ = parent; }
    void write_state(bool state) override {
        this->parent_->set_secondary_flame(state);
        this->publish_state(state);
    }
 protected:
    ProFlame2Component *parent_;
};

}  // namespace proflame2
}  // namespace esphome
