#include "aw9523b.h"
#include "esphome/core/gpio.h"
#include "esphome/core/log.h"
#include "esphome/core/progmem.h"

namespace esphome::aw9523b {

static const uint8_t AW9523B_INPUT_P0 = 0x00;     // P0 Input Status, read only
static const uint8_t AW9523B_INPUT_P1 = 0x01;     // P1 Input Status, read only
static const uint8_t AW9523B_OUTPUT_P0 = 0x02;    // P0 Output Status, R/W
static const uint8_t AW9523B_OUTPUT_P1 = 0x03;    // P1 Output Status, R/W
static const uint8_t AW9523B_CONFIG_P0 = 0x04;    // P0 Input/Output configuration, R/W
static const uint8_t AW9523B_CONFIG_P1 = 0x05;    // P1 Input/Output configuration, R/W
static const uint8_t AW9523B_INT_P0 = 0x06;       // P0 Interrupt, R/W
static const uint8_t AW9523B_INT_P1 = 0x07;       // P1 Interrupt, R/W
static const uint8_t AW9523B_ID = 0x10;           // ID chip, read only
static const uint8_t AW9523B_CTL = 0x11;          // Global Control Register
static const uint8_t AW9523B_LED_MODE_P0 = 0x12;  // LED Mode Switch for P0
static const uint8_t AW9523B_LED_MODE_P1 = 0x13;  // LED Mode Switch for P1
static constexpr uint8_t AW9523B_DIM_P1_0 = 0x20;
static constexpr uint8_t AW9523B_DIM_P0_0 = 0x24;
static constexpr uint8_t AW9523B_DIM_P1_4 = 0x2C;

static const uint8_t AW9523B_SW_RST = 0x7F;  // software reset

ESPHOME_LOG_TAG(TAG, "aw9523b");

#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_DEBUG
PROGMEM_STRING_TABLE(P0DriveModeStrings, "OPEN_DRAIN", "PUSH_PULL");

static const LogString *p0_drive_mode_to_string(AW9523BP0DriveMode mode) {
  return P0DriveModeStrings::get_log_str(mode, 0);
}

PROGMEM_STRING_TABLE(LEDMaxCurrentStrings, "37mA", "27.75mA", "18.5mA", "9.25mA");

static const LogString *led_max_current_to_string(AW9523BLEDMaxCurrent current) {
  return LEDMaxCurrentStrings::get_log_str(current, 0);
}
#endif

#define AW9523B_ERROR_FAILED(err) \
  if (!(err)) { \
    this->mark_failed(); \
    return; \
  }

void AW9523BComponent::setup() {
  ESP_LOGD(TAG, "Setting up AW9523B IO expander.");

  // software reset
  if (this->reset_) {
    AW9523B_ERROR_FAILED(this->write_byte(AW9523B_SW_RST, 0x00));
    delay(5);
  }

  // read chip id for validation
  uint8_t chip_id;
  AW9523B_ERROR_FAILED(this->read_byte(AW9523B_ID, &chip_id));

  if (chip_id != 0x23) {
    ESP_LOGE(TAG, "Error reading chip id, expect '0x23', got '0x%X'", chip_id);
    this->mark_failed();
    return;
  }

  ESP_LOGD(TAG, "Read chip id: 0x%X", chip_id);

  // Interrupts are enabled by default after reset. Disable every pin first,
  // then selectively enable configured input pins.
  this->interrupt_mask_ = 0xFFFF;
  uint8_t interrupt_data[2] = {0xFF, 0xFF};
  AW9523B_ERROR_FAILED(this->write_bytes(AW9523B_INT_P0, interrupt_data, sizeof(interrupt_data)));

  // read global control register
  uint8_t ctl_data;
  AW9523B_ERROR_FAILED(this->read_byte(AW9523B_CTL, &ctl_data));

  // configure pad 0 open-drain or push-pull mode
  if (this->p0_drive_mode_ == AW9523BP0DriveMode::PUSH_PULL) {
    ctl_data |= (1 << 4);  //  push-pull
  } else {
    ctl_data &= ~(1 << 4);  //  open-drain
  }

  // set led max current
  ctl_data &= ~0x03;
  ctl_data |= static_cast<uint8_t>(this->led_max_current_) & 0x03;

  // write global control register
  AW9523B_ERROR_FAILED(this->write_byte(AW9523B_CTL, ctl_data));

  if (!this->reset_) {
    // No reset performed; read current register values
    if (!this->read_gpio_modes_()) {
      this->mark_failed();
      return;
    }
    if (!this->read_led_modes_()) {
      this->mark_failed();
      return;
    }
  }

  // Preserve the actual output latch state, including the address-dependent power-on values.
  if (!this->read_gpio_outputs_()) {
    this->mark_failed();
    return;
  }

  // Attach GPIO Interrupt
  if (this->interrupt_pin_ != nullptr) {
    this->interrupt_pin_->setup();
    // INTN is active-low, so use a falling edge to wake the component loop.
    this->interrupt_pin_->attach_interrupt(&AW9523BComponent::gpio_intr, this, gpio::INTERRUPT_FALLING_EDGE);
    // Don't invalidate cache on read — only invalidate when interrupt fires
    this->set_invalidate_on_read_(false);
  }
  this->update_loop_();

  ESP_LOGI(TAG, "AW9523B setup complete.");
}

void AW9523BComponent::loop() {
  this->reset_pin_cache_();

  if (this->interrupt_pin_ != nullptr && this->interrupt_pin_->digital_read()) {
    // Keep polling if any configured input does not use the hardware interrupt.
    this->update_loop_();
  }
}

void AW9523BComponent::dump_config() {
  ESP_LOGCONFIG(TAG,
                "AW9523B: \n"
                "  P0 drive mode: %s\n"
                "  LED Max Current: %s",
                LOG_STR_ARG(p0_drive_mode_to_string(this->p0_drive_mode_)),
                LOG_STR_ARG(led_max_current_to_string(this->led_max_current_)));
  LOG_PIN("  Interrupt Pin: ", this->interrupt_pin_);
  LOG_I2C_DEVICE(this);
  if (this->is_failed()) {
    ESP_LOGE(TAG, ESP_LOG_MSG_COMM_FAIL);
  }
}

void IRAM_ATTR AW9523BComponent::gpio_intr(AW9523BComponent *arg) { arg->enable_loop_soon_any_context(); }

// AW9523 only support INPUT and OUTPUT in GPIO mode
void AW9523BComponent::pin_mode(uint8_t pin, gpio::Flags flags) {
  uint16_t mask = 1 << pin;

  if (flags & gpio::FLAG_INPUT) {
    this->mode_mask_ |= mask;
    this->input_pin_mask_ |= mask;
  } else if (flags & gpio::FLAG_OUTPUT) {
    this->mode_mask_ &= ~mask;
    this->input_pin_mask_ &= ~mask;
  }
  this->write_gpio_modes_();
}

bool AW9523BComponent::read_gpio_outputs_() {
  if (this->is_failed()) {
    return false;
  }

  uint8_t data[2];

  if (!this->read_bytes(AW9523B_OUTPUT_P0, data, 2)) {
    this->status_set_warning(LOG_STR("Failed to read output registers"));
    return false;
  }

  this->status_clear_warning();

  this->output_mask_ = data[0] | (data[1] << 8);

  return true;
}

bool AW9523BComponent::read_gpio_modes_() {
  if (this->is_failed()) {
    return false;
  }

  uint8_t data[2];

  if (!this->read_bytes(AW9523B_CONFIG_P0, data, 2)) {
    this->status_set_warning(LOG_STR("Failed to read GPIO modes"));
    return false;
  }

  this->status_clear_warning();

  this->mode_mask_ = data[0] | (data[1] << 8);

  return true;
}

bool AW9523BComponent::write_gpio_modes_() {
  if (this->is_failed()) {
    return false;
  }

  if (!this->write_bytes(AW9523B_CONFIG_P0, reinterpret_cast<uint8_t *>(&this->mode_mask_), 2)) {
    this->status_set_warning(LOG_STR("Failed to write GPIO modes"));
    return false;
  }

  this->status_clear_warning();

  return true;
}

bool AW9523BComponent::digital_read_hw(uint8_t pin) {
  if (this->is_failed()) {
    return false;
  }

  uint8_t data[2];

  if (!this->read_bytes(AW9523B_INPUT_P0, data, 2)) {
    this->status_set_warning(LOG_STR("Failed to read input registers"));
    return false;
  }

  this->status_clear_warning();

  this->input_mask_ = data[0] | (data[1] << 8);
  return true;
}

bool AW9523BComponent::digital_read_cache(uint8_t pin) { return (this->input_mask_ & (1 << pin)); }

void AW9523BComponent::digital_write_hw(uint8_t pin, bool value) {
  if (this->is_failed()) {
    return;
  }

  uint16_t new_mask = this->output_mask_;

  if (value)
    new_mask |= (1 << pin);  // bit set
  else
    new_mask &= ~(1 << pin);  // bit clear

  if (!this->write_bytes(AW9523B_OUTPUT_P0, reinterpret_cast<uint8_t *>(&new_mask), 2)) {
    this->status_set_warning(LOG_STR("Failed to set pin for output"));
    return;
  }

  this->output_mask_ = new_mask;

  this->status_clear_warning();

#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE
  ESP_LOGV(TAG, "Wrote GPIO output: 0b" BYTE_TO_BINARY_PATTERN " " BYTE_TO_BINARY_PATTERN,
           BYTE_TO_BINARY(this->output_mask_ >> 8), BYTE_TO_BINARY(this->output_mask_ & 0x00FF));
#endif
}

bool AW9523BComponent::setup_led_mode(uint8_t pin) {
  if (this->is_failed()) {
    return false;
  }
  if (pin >= 16) {
    this->status_set_warning(LOG_STR("Invalid LED pin"));
    ESP_LOGE(TAG, "Invalid LED pin %u", pin);
    return false;
  }

  this->led_mode_mask_ &= ~(1 << pin);

  if (!this->write_bytes(AW9523B_LED_MODE_P0, reinterpret_cast<uint8_t *>(&this->led_mode_mask_), 2)) {
    this->status_set_warning(LOG_STR("Failed to write LED mode"));
    return false;
  }

  this->status_clear_warning();
  return true;
}

void AW9523BComponent::write_led_current(uint8_t pin, uint8_t current) {
  if (this->is_failed()) {
    return;
  }

  if (pin >= 16) {
    this->status_set_warning(LOG_STR("Invalid LED pin"));
    ESP_LOGW(TAG, "Invalid LED pin %u", pin);
    return;
  }

  uint8_t dim_register;
  if (pin < 8) {
    dim_register = AW9523B_DIM_P0_0 + pin;
  } else if (pin < 12) {
    dim_register = AW9523B_DIM_P1_0 + (pin - 8);
  } else {
    dim_register = AW9523B_DIM_P1_4 + (pin - 12);
  }

  if (!this->write_byte(dim_register, current)) {
    this->status_set_warning(LOG_STR("Failed to set LED current"));
    return;
  }
  this->status_clear_warning();
}

bool AW9523BComponent::read_led_modes_() {
  if (this->is_failed()) {
    return false;
  }

  uint8_t data[2];

  if (!this->read_bytes(AW9523B_LED_MODE_P0, data, 2)) {
    this->status_set_warning(LOG_STR("Failed to read led mode registers"));
    return false;
  }

  this->led_mode_mask_ = data[0] | (data[1] << 8);
  this->status_clear_warning();

  return true;
}

void AW9523BComponent::setup_gpio_interrupt(uint8_t pin, bool enable) {
  if (this->is_failed())
    return;

  const uint16_t pin_mask = 1 << pin;
  // AW9523B interrupt-mask bits are active-low: clear the bit to enable, set it to disable.
  const uint16_t new_mask = enable ? this->interrupt_mask_ & ~pin_mask : this->interrupt_mask_ | pin_mask;
  const uint8_t data[2] = {static_cast<uint8_t>(new_mask), static_cast<uint8_t>(new_mask >> 8)};
  if (!this->write_bytes(AW9523B_INT_P0, data, sizeof(data))) {
    this->status_set_warning(LOG_STR("Failed to set interrupt configuration"));
    ESP_LOGW(TAG, "Failed to set interrupt configuration for pin %u", pin);
    this->update_loop_();
    return;
  }

  this->interrupt_mask_ = new_mask;
  this->status_clear_warning();
  this->update_loop_();
}

void AW9523BComponent::update_loop_() {
  // Poll inputs without an enabled hardware interrupt, or all inputs if no INT pin is configured.
  if (this->input_pin_mask_ != 0 &&
      (this->interrupt_pin_ == nullptr || (this->input_pin_mask_ & this->interrupt_mask_) != 0)) {
    this->enable_loop();
  } else {
    this->disable_loop();
  }
}

void AW9523BGPIOPin::setup() { this->pin_mode(this->flags_); }
void AW9523BGPIOPin::pin_mode(gpio::Flags flags) {
  this->flags_ = flags;
  this->parent_->pin_mode(this->pin_, flags);
  this->parent_->setup_gpio_interrupt(this->pin_, (flags & gpio::FLAG_INPUT) && this->use_interrupt_);
}
bool AW9523BGPIOPin::digital_read() { return this->parent_->digital_read(this->pin_) != this->inverted_; }
void AW9523BGPIOPin::digital_write(bool value) { this->parent_->digital_write(this->pin_, value != this->inverted_); }

size_t AW9523BGPIOPin::dump_summary(char *buffer, size_t len) const {
  return snprintf(buffer, len, "%u via AW9523B", this->pin_);
}

}  // namespace esphome::aw9523b
