#pragma once

// Minimal Arduino API shim, so the vendored ELECHOUSE CC1101 driver compiles
// unmodified under the ESP-IDF framework. Only the handful of symbols that
// driver actually reaches for are provided: pinMode / digitalWrite /
// digitalRead, an SPI object, map(), bitRead(), delay() and delayMicroseconds().
//
// SPI is bit-banged in software rather than routed through esp_spi_master. The
// driver polls MISO as a plain GPIO to handshake with the chip -- see the
// `while (digitalRead(MISO_PIN));` waits in ELECHOUSE_CC1101_SRC_DRV.cpp -- and
// reading that pin while a hardware SPI peripheral owns it is the fragile part
// of any hardware-SPI port. Packets are 20 bytes and sent a few times a day, so
// there is nothing to gain from the peripheral anyway.

#include <cstdint>
#include <string.h>  // strlen(), which the driver otherwise gets via Arduino.h

#include <driver/gpio.h>
#include <esp_rom_sys.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

using byte = uint8_t;

#ifndef HIGH
#define HIGH 1
#endif
#ifndef LOW
#define LOW 0
#endif
#ifndef INPUT
#define INPUT 0
#endif
#ifndef OUTPUT
#define OUTPUT 1
#endif

// Inputs get a pull-down so that a missing or unpowered CC1101 reads back as
// zero instead of floating. Without it, the driver's unbounded
// `while (digitalRead(MISO_PIN));` chip-ready waits can hang forever on a
// wiring fault, which surfaces as a boot loop rather than the far more useful
// "CC1101 not detected" that quietcool.cpp already logs. The chip drives both
// MISO and GDO0 actively when it is present, so the pull-down is invisible then.
//
// gpio_config() rather than gpio_reset_pin(), because the driver calls
// SpiStart() -- and therefore pinMode() -- before every single transaction, and
// gpio_reset_pin() would briefly float CS on each one.
inline void pinMode(uint8_t pin, uint8_t mode) {
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << pin;
  cfg.mode = (mode == OUTPUT) ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT;
  cfg.pull_up_en = GPIO_PULLUP_DISABLE;
  cfg.pull_down_en = (mode == OUTPUT) ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE;
  cfg.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&cfg);
}

inline void digitalWrite(uint8_t pin, uint8_t value) {
  gpio_set_level(static_cast<gpio_num_t>(pin), value ? 1 : 0);
}

inline int digitalRead(uint8_t pin) { return gpio_get_level(static_cast<gpio_num_t>(pin)); }

inline void delayMicroseconds(uint32_t us) { esp_rom_delay_us(us); }

// vTaskDelay() on its own truncates to whole scheduler ticks (10 ms by default),
// which would turn the driver's delay(1) in Reset() into no delay at all. Yield
// the whole ticks, then busy-wait the remainder.
inline void delay(uint32_t ms) {
  uint32_t ticks = ms / portTICK_PERIOD_MS;
  uint32_t remainder_ms = ms % portTICK_PERIOD_MS;
  if (ticks > 0)
    vTaskDelay(ticks);
  if (remainder_ms > 0)
    esp_rom_delay_us(remainder_ms * 1000);
}

// Arduino's map() works on longs. The driver leans on that truncation when it
// passes the float `MHz` in (see the setMHZ() calibration lookups), so keep the
// integer signature rather than templating it.
inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

#ifndef bitRead
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#endif

// Software SPI, mode 0 (CPOL=0, CPHA=0), MSB first -- what the CC1101 expects.
// MOSI changes while SCK is low; MISO is sampled on the rising edge.
class SPIClass {
 public:
  void begin(uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t ss) {
    this->sck_ = sck;
    this->miso_ = miso;
    this->mosi_ = mosi;
    (void) ss;  // the driver drives CS itself, via digitalWrite(SS_PIN, ...)
  }

  // The driver calls these around every transaction; with software SPI there is
  // no peripheral to claim or release.
  void begin() {}
  void end() {}
  void beginTransaction() {}
  void endTransaction() {}

  uint8_t transfer(uint8_t out) {
    // Init() deliberately leaves SCK high before the first transfer. Arduino's
    // hardware peripheral would have idled it low when it took the pin over, so
    // match that here or the first bit gets clocked twice. A falling edge is
    // never the sampling edge in mode 0, so doing this after CS is already
    // asserted is harmless.
    digitalWrite(this->sck_, LOW);

    uint8_t in = 0;
    for (int bit = 7; bit >= 0; bit--) {
      digitalWrite(this->mosi_, (out >> bit) & 1);
      esp_rom_delay_us(HALF_PERIOD_US);
      digitalWrite(this->sck_, HIGH);
      in = (in << 1) | digitalRead(this->miso_);
      esp_rom_delay_us(HALF_PERIOD_US);
      digitalWrite(this->sck_, LOW);
    }
    return in;
  }

 private:
  // ~250 kHz. Far below the CC1101's 6.5 MHz burst limit, and fast enough that a
  // 20-byte packet clocks out in well under a millisecond.
  static constexpr uint32_t HALF_PERIOD_US = 2;

  uint8_t sck_{};
  uint8_t miso_{};
  uint8_t mosi_{};
};

inline SPIClass SPI;
