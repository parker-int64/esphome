#include "aw9523b_output.h"
#include "esphome/components/output/float_output.h"
#include "esphome/core/log.h"

namespace esphome::aw9523b {

ESPHOME_LOG_TAG(TAG, "aw9523b.output");

void AW9523BFloatOutput::setup() {
  // Setup the Pin
  if (this->pin_ != nullptr) {
    this->pin_->setup();
  }
  this->parent_->setup_led_mode(this->pin_->get_pin());
}

void AW9523BFloatOutput::dump_config() {
  ESP_LOGCONFIG(TAG,
                "AW9523B Float Output:\n"
                "  Pin: %d",
                this->pin_->get_pin());
  LOG_FLOAT_OUTPUT(this);
}

void AW9523BFloatOutput::write_state(float state) {
  uint8_t current = static_cast<uint8_t>(roundf(state * 255.0f));  // 0 - 255
  this->parent_->write_led_current(this->pin_->get_pin(), current);
}

}  // namespace esphome::aw9523b
