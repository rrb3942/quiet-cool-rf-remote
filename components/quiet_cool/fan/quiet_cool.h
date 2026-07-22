#pragma once

#include "esphome/components/fan/fan.h"
#include "esphome/core/component.h"
#include "quietcool.h"
#include <memory>

namespace esphome {
    namespace quiet_cool {

        // Deliberately not an spi::SPIDevice. The vendored CC1101 driver owns its
        // own pins and does its own (software) SPI, so an ESPHome SPI bus was only
        // ever decorative here -- and letting both drive CS invites conflicts.
        class QuietCoolFan :
	    public Component,
	    public fan::Fan
	{
        public:

            void dump_config() override;
            fan::FanTraits get_traits() override;
            void setup() override;  // initialise radio
            float get_setup_priority() const override { return setup_priority::BUS; }
            void set_pins(uint8_t csn, uint8_t gdo0, uint8_t gdo2, uint8_t sck, uint8_t miso, uint8_t mosi) {
                this->csn_pin_ = csn;
                this->gdo0_pin_ = gdo0;
                this->gdo2_pin_ = gdo2;
                this->sck_pin_ = sck;
                this->miso_pin_ = miso;
                this->mosi_pin_ = mosi;
                this->pins_set_ = true;
            }
	    void set_frequencies(float center_freq_mhz, float deviation_khz) {
		this->center_freq_mhz = center_freq_mhz;
		this->deviation_khz = deviation_khz;
	    }

        protected:
            void control(const fan::FanCall &call) override;
            void write_state_();
        private:
            std::unique_ptr<QuietCool> qc_;

            uint8_t csn_pin_{};
            uint8_t gdo0_pin_{};
            uint8_t gdo2_pin_{};
            uint8_t sck_pin_{};
            uint8_t miso_pin_{};
            uint8_t mosi_pin_{};
	    float center_freq_mhz{433.897};
	    float deviation_khz{10};
            float speed_{0.0f};
            bool pins_set_{false};
            std::array<uint8_t, 7> remote_id_{{0x2D, 0xD4, 0x06, 0xCB, 0x00, 0xF7, 0xF2}};
        public:
            void set_remote_id(const std::vector<uint8_t> &remote_id) {
                for (size_t i = 0; i < 7 && i < remote_id.size(); ++i) remote_id_[i] = remote_id[i];
            }
        };

    }  // namespace quiet_cool
}  // namespace esphome
