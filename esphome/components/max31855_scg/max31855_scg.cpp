#include "max31855_scg.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace max31855_scg {

static const char *const TAG = "max31855_scg";

void MAX31855SCGSensor::setup() { this->spi_setup(); }

void MAX31855SCGSensor::dump_config() {
  ESP_LOGCONFIG(TAG, "MAX31855 with kiln SCG tolerance:");
  LOG_PIN("  CS Pin: ", this->cs_);
  LOG_UPDATE_INTERVAL(this);
  LOG_SENSOR("  ", "Thermocouple", this);
  if (this->reference_ != nullptr) LOG_SENSOR("  ", "Reference", this->reference_);
}

void MAX31855SCGSensor::update() {
  // A rising CS edge starts a new thermocouple conversion.
  this->enable();
  delay(1);
  this->disable();
  this->set_timeout("read", 220, [this]() { this->read_data_(); });
}

void MAX31855SCGSensor::read_data_() {
  this->enable();
  delay(1);
  uint8_t data[4];
  this->read_array(data, sizeof(data));
  this->disable();

  const uint32_t raw = encode_uint32(data[0], data[1], data[2], data[3]);
  if (raw == 0xFFFFFFFF) {
    ESP_LOGE(TAG, "No data received from MAX31855; check SPI wiring");
    this->publish_state(NAN);
    if (this->reference_ != nullptr) this->reference_->publish_state(NAN);
    this->status_set_error();
    return;
  }
  this->status_clear_error();

  int16_t internal_raw = (raw & 0x0000FFF0) >> 4;
  if (internal_raw & 0x0800) internal_raw |= 0xF000;
  if (this->reference_ != nullptr) {
    this->reference_->publish_state(static_cast<float>(internal_raw) * 0.0625f);
  }

  const uint8_t faults = raw & 0x07;
  // Preserve the original kiln workaround for a false short-to-ground (SCG)
  // indication. Open circuit and short-to-VCC still fail closed.
  const bool scg_only = faults == 0x02;
  if ((faults != 0 && !scg_only) || ((raw & 0x00010000) != 0 && !scg_only)) {
    ESP_LOGW(TAG, "Thermocouple fault flags 0x%02X; heating must remain off", faults);
    this->publish_state(NAN);
    this->status_set_warning();
    return;
  }
  if (scg_only) {
    ESP_LOGD(TAG, "Ignoring SCG flag as in the original kiln firmware");
    this->status_set_warning();
  } else {
    this->status_clear_warning();
  }

  int16_t temperature_raw = (raw & 0xFFFC0000) >> 18;
  if (temperature_raw & 0x2000) temperature_raw |= 0xC000;
  this->publish_state(static_cast<float>(temperature_raw) * 0.25f);
}

}  // namespace max31855_scg
}  // namespace esphome
