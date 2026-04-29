#pragma once

// CC1101 register-address and strobe constants. Extracted from
// proflame2_cc1101.h so files that only need register names don't have to
// pull in the full ProFlame2Component declaration.

#include <cstdint>

namespace esphome {
namespace proflame2 {

// CC1101 Register definitions
static const uint8_t CC1101_IOCFG2 = 0x00;
static const uint8_t CC1101_IOCFG1 = 0x01;
static const uint8_t CC1101_IOCFG0 = 0x02;
static const uint8_t CC1101_FIFOTHR = 0x03;
static const uint8_t CC1101_SYNC1 = 0x04;
static const uint8_t CC1101_SYNC0 = 0x05;
static const uint8_t CC1101_PKTLEN = 0x06;
static const uint8_t CC1101_PKTCTRL1 = 0x07;
static const uint8_t CC1101_PKTCTRL0 = 0x08;
static const uint8_t CC1101_ADDR = 0x09;
static const uint8_t CC1101_CHANNR = 0x0A;
static const uint8_t CC1101_FSCTRL1 = 0x0B;
static const uint8_t CC1101_FSCTRL0 = 0x0C;
static const uint8_t CC1101_FREQ2 = 0x0D;
static const uint8_t CC1101_FREQ1 = 0x0E;
static const uint8_t CC1101_FREQ0 = 0x0F;
static const uint8_t CC1101_MDMCFG4 = 0x10;
static const uint8_t CC1101_MDMCFG3 = 0x11;
static const uint8_t CC1101_MDMCFG2 = 0x12;
static const uint8_t CC1101_MDMCFG1 = 0x13;
static const uint8_t CC1101_MDMCFG0 = 0x14;
static const uint8_t CC1101_DEVIATN = 0x15;

// State machine / calibration / analog front-end
static const uint8_t CC1101_MCSM2 = 0x16;
static const uint8_t CC1101_MCSM1 = 0x17;
static const uint8_t CC1101_MCSM0 = 0x18;
static const uint8_t CC1101_FOCCFG = 0x19;
static const uint8_t CC1101_BSCFG = 0x1A;
static const uint8_t CC1101_AGCCTRL2 = 0x1B;
static const uint8_t CC1101_AGCCTRL1 = 0x1C;
static const uint8_t CC1101_AGCCTRL0 = 0x1D;
static const uint8_t CC1101_FREND1 = 0x21;
static const uint8_t CC1101_FREND0 = 0x22;
static const uint8_t CC1101_FSCAL3 = 0x23;
static const uint8_t CC1101_FSCAL2 = 0x24;
static const uint8_t CC1101_FSCAL1 = 0x25;
static const uint8_t CC1101_FSCAL0 = 0x26;
static const uint8_t CC1101_TEST2 = 0x2C;
static const uint8_t CC1101_TEST1 = 0x2D;
static const uint8_t CC1101_TEST0 = 0x2E;

// Additional registers needed for TX state management
static const uint8_t CC1101_MARCSTATE = 0x35;  // Status register (read with 0xC0)
static const uint8_t CC1101_TXBYTES = 0x3A;    // Status register (read with 0xC0)
static const uint8_t CC1101_PATABLE = 0x3E;    // PA table

// CC1101 Strobe commands
static const uint8_t CC1101_SRES = 0x30;
static const uint8_t CC1101_SFSTXON = 0x31;
static const uint8_t CC1101_SXOFF = 0x32;
static const uint8_t CC1101_SCAL = 0x33;
static const uint8_t CC1101_SRX = 0x34;
static const uint8_t CC1101_STX = 0x35;
static const uint8_t CC1101_SIDLE        = 0x36;
static const uint8_t CC1101_SFTX         = 0x3A;  // Flush TX FIFO strobe
static const uint8_t CC1101_SFRX         = 0x3B;  // Flush RX FIFO strobe
static const uint8_t CC1101_TXFIFO_BURST = 0x7F;  // TX FIFO burst-write SPI address

}  // namespace proflame2
}  // namespace esphome
