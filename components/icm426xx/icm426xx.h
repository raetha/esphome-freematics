#pragma once

#include <memory>

#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif

#include "motion.h"

namespace esphome::icm426xx {

/// TDK's ICM-426xx motion sensors (the ICM-42627 on the Freematics ONE+ Model
/// B). Awake, the accelerometer streams 100 samples a second through its FIFO:
/// a motion binary sensor is on while the device is being moved, an impact
/// sensor reports each impact's peak, and a temperature sensor reads the chip.
/// Before deep sleep, arm_wake() leaves it in low-power wake-on-motion, which
/// woke_by_motion() reads after the next start.
class ICM426xx : public PollingComponent, public i2c::I2CDevice {
 public:
  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_motion_threshold(float g) { this->motion_g_ = g; }
  void set_impact_threshold(float g) { this->impact_g_ = g; }
  void set_motion_hold(uint32_t ms) { this->motion_hold_ms_ = ms; }
#ifdef USE_BINARY_SENSOR
  void set_motion_binary_sensor(binary_sensor::BinarySensor *s) { this->motion_sensor_ = s; }
#endif
#ifdef USE_SENSOR
  void set_temperature_sensor(sensor::Sensor *s) { this->temperature_sensor_ = s; }
  void set_impact_sensor(sensor::Sensor *s) { this->impact_sensor_ = s; }
#endif

  /// Whether a supported sensor answered.
  bool is_present() const { return this->present_; }
  /// Loop time of the last movement; 0: none since start.
  uint32_t last_motion_ms() const { return this->last_motion_ms_; }

  /// After a timer wake from deep sleep, before setup(): whether the sensor's
  /// wake-on-motion flag is set (reading clears it). The next setup() counts
  /// it as movement.
  bool woke_by_motion();
  /// Before deep sleep: low-power wake-on-motion at this threshold (0: the
  /// accelerometer off). Takes about a quarter of a second; wake_armed() says when done.
  void arm_wake(float threshold_g);
  bool wake_armed() const { return this->wake_state_ == WakeState::ARMED; }

 protected:
  bool detect_();
  bool stream_();
  void read_fifo_();
  void moved_(uint32_t now);
  bool put_(uint8_t reg, uint8_t value);

  enum class WakeState : uint8_t { NONE, ARMING, ARMED, FAILED };

  float motion_g_{0.1f};
  float impact_g_{4.0f};
  uint32_t motion_hold_ms_{10000};
  bool present_{false};
  bool streaming_{false};
  bool woke_moving_{false};
  WakeState wake_state_{WakeState::NONE};
  uint32_t last_motion_ms_{0};
  uint32_t last_read_ms_{0};
  uint8_t fifo_errors_{0};
  uint8_t settle_{0};  // samples still to ignore after the accelerometer starts
  motion::Analyzer analyzer_;
  std::unique_ptr<uint8_t[]> fifo_;  // NOLINT(cppcoreguidelines-avoid-c-arrays)
#ifdef USE_BINARY_SENSOR
  binary_sensor::BinarySensor *motion_sensor_{nullptr};
#endif
#ifdef USE_SENSOR
  sensor::Sensor *temperature_sensor_{nullptr};
  sensor::Sensor *impact_sensor_{nullptr};
#endif
};

template<typename... Ts> class ArmWakeAction : public Action<Ts...>, public Parented<ICM426xx> {
 public:
  TEMPLATABLE_VALUE(float, threshold)
  void play(const Ts &...x) override { this->parent_->arm_wake(this->threshold_.value(x...)); }
};

}  // namespace esphome::icm426xx
