#pragma once

#include "esphome/components/output/float_output.h"
#include "../aw9523b.h"
#include "esphome/core/gpio.h"

namespace esphome::aw9523b {

class AW9523BFloatOutput : public output::FloatOutput, public Component, public Parented<AW9523BComponent> {
 public:
  void setup() override;
  void dump_config() override;
  void set_pin(AW9523BGPIOPin *pin) { this->pin_ = pin; }

 protected:
  void write_state(float state) override;
  AW9523BGPIOPin *pin_{nullptr};
};

}  // namespace esphome::aw9523b
