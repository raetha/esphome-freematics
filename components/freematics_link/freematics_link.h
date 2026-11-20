#pragma once

#include "esphome/components/uart/uart.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif

#include "link_parse.h"

namespace esphome::freematics_link {

using ::freematics::Fix;

/// The commands this component may send. All are answered by the co-processor
/// itself; none reaches the vehicle bus. There is deliberately no way to send
/// anything else.
enum class Command : uint8_t { NONE, IDENTIFY, BATTERY, GNSS_ON, GNSS_READ, GNSS_OFF, LOW_POWER };

class FreematicsLink final : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  // Just after sensors (DATA): an early on_boot check can then read the battery
  // voltage and go back to sleep before this resets the co-processor (standby).
  float get_setup_priority() const override { return setup_priority::DATA - 1.0f; }

  void set_reset_pin(GPIOPin *pin) { this->reset_pin_ = pin; }
  void set_gnss_interval(uint32_t ms) { this->gnss_interval_ = ms; }
  void set_battery_interval(uint32_t ms) { this->battery_interval_ = ms; }
#ifdef USE_SENSOR
  void set_latitude_sensor(sensor::Sensor *s) { this->latitude_ = s; }
  void set_longitude_sensor(sensor::Sensor *s) { this->longitude_ = s; }
  void set_altitude_sensor(sensor::Sensor *s) { this->altitude_ = s; }
  void set_speed_sensor(sensor::Sensor *s) { this->speed_ = s; }
  void set_course_sensor(sensor::Sensor *s) { this->course_ = s; }
  void set_satellites_sensor(sensor::Sensor *s) { this->satellites_ = s; }
  void set_hdop_sensor(sensor::Sensor *s) { this->hdop_ = s; }
  void set_battery_voltage_sensor(sensor::Sensor *s) { this->battery_voltage_ = s; }
#endif
#ifdef USE_BINARY_SENSOR
  void set_fix_binary_sensor(binary_sensor::BinarySensor *s) { this->fix_ = s; }
#endif
#ifdef USE_TEXT_SENSOR
  void set_version_text_sensor(text_sensor::TextSensor *s) { this->version_ = s; }
#endif

  /// The latest usable fix, and whether there is one.
  bool has_fix() const { return this->has_fix_; }
  const Fix &last_fix() const { return this->last_fix_; }
  /// The co-processor's version (14 for 1.4), 0 until known.
  int version() const { return this->version_number_; }

  /// Before the ESP32 sleeps: switch the GNSS off (ATGPSOFF) and put the
  /// co-processor in its low-power mode (ATLP), as the stock firmware's
  /// standby does; polling stops. A reset (the next boot) wakes it again.
  void enter_standby();
  /// True once both commands have been sent (answered or not).
  bool in_standby() const { return this->state_ == State::STANDBY; }

  Trigger<Fix> *get_fix_trigger() { return &this->fix_trigger_; }

 protected:
  void send_(Command cmd);
  void log_no_reply_();
  void handle_reply_(Command cmd, const char *reply);
  void handle_fix_(const char *reply);

  GPIOPin *reset_pin_{nullptr};
  uint32_t gnss_interval_{2000};
  uint32_t battery_interval_{60000};
#ifdef USE_SENSOR
  sensor::Sensor *latitude_{nullptr};
  sensor::Sensor *longitude_{nullptr};
  sensor::Sensor *altitude_{nullptr};
  sensor::Sensor *speed_{nullptr};
  sensor::Sensor *course_{nullptr};
  sensor::Sensor *satellites_{nullptr};
  sensor::Sensor *hdop_{nullptr};
  sensor::Sensor *battery_voltage_{nullptr};
#endif
#ifdef USE_BINARY_SENSOR
  binary_sensor::BinarySensor *fix_{nullptr};
#endif
#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *version_{nullptr};
#endif

  enum class State : uint8_t {
    RESETTING,
    IDENTIFYING,
    GNSS_STARTING,
    RUNNING,
    STANDBY_GNSS_OFF,
    STANDBY_LOW_POWER,
    STANDBY
  };
  State state_{State::RESETTING};
  Command pending_{Command::NONE};
  uint32_t sent_at_{0};
  uint32_t state_since_{0};
  uint32_t last_gnss_{0};
  uint32_t last_battery_{0};
  uint8_t identify_tries_{0};
  // Diagnostics: every byte received since boot, and GNSS reads in a row with no reply.
  uint32_t rx_total_{0};
  uint32_t gnss_replies_{0};
  uint16_t gnss_misses_{0};
  bool fix_now_{false};  // the latest reply held a usable fix

  char rx_[256]{};
  size_t rx_len_{0};

  bool has_fix_{false};
  Fix last_fix_{};
  int version_number_{0};

  Trigger<Fix> fix_trigger_;
};

}  // namespace esphome::freematics_link
