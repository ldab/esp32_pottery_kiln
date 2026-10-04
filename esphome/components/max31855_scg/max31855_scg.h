#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/spi/spi.h"

namespace esphome {
namespace max31855_scg {

class MAX31855SCGSensor final
    : public sensor::Sensor,
      public PollingComponent,
      public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_HIGH,
                            spi::CLOCK_PHASE_TRAILING, spi::DATA_RATE_4MHZ> {
 public:
  void set_reference_sensor(sensor::Sensor *reference) { reference_ = reference; }
  void setup() override;
  void dump_config() override;
  void update() override;

 protected:
  void read_data_();
  sensor::Sensor *reference_{nullptr};
};

}  // namespace max31855_scg
}  // namespace esphome
