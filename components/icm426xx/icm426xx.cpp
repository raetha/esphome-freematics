#include "icm426xx.h"

#include <algorithm>
#include <cmath>

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::icm426xx {

static const char *const TAG = "icm426xx";

// Register map (bank 0 unless noted): TDK's ICM-42627 datasheet.
static constexpr uint8_t REG_TEMP_DATA1 = 0x1D;
static constexpr uint8_t REG_FIFO_CONFIG = 0x16;
static constexpr uint8_t REG_FIFO_COUNTH = 0x2E;
static constexpr uint8_t REG_FIFO_DATA = 0x30;
static constexpr uint8_t REG_INT_STATUS2 = 0x37;  // wake-on-motion flags, cleared by reading
static constexpr uint8_t REG_SIGNAL_PATH_RESET = 0x4B;
static constexpr uint8_t REG_PWR_MGMT0 = 0x4E;
static constexpr uint8_t REG_ACCEL_CONFIG0 = 0x50;
static constexpr uint8_t REG_GYRO_ACCEL_CONFIG0 = 0x52;
static constexpr uint8_t REG_SMD_CONFIG = 0x57;
static constexpr uint8_t REG_FIFO_CONFIG1 = 0x5F;
static constexpr uint8_t REG_INT_SOURCE1 = 0x66;
static constexpr uint8_t REG_WHO_AM_I = 0x75;
static constexpr uint8_t REG_BANK_SEL = 0x76;
static constexpr uint8_t REG_ACCEL_WOM_X_THR = 0x4A;  // bank 4, then Y and Z

static constexpr uint8_t WHO_AM_I_ICM42627 = 0x20;

static constexpr float RATE_HZ = 100;
static constexpr float G_PER_LSB = 16.0f / 32768.0f;  // +-16 g
static constexpr size_t PACKET = 8;                   // FIFO packet 1: header, accel x/y/z (16-bit), temperature
static constexpr size_t FIFO_READ = 32 * PACKET;      // a third of a second at most per read
// The first samples after the accelerometer starts are its start-up, not
// movement: a quarter of a second of them is left out.
static constexpr uint8_t SETTLE_SAMPLES = 25;

static uint32_t now_ms() { return App.get_loop_component_start_time(); }

bool ICM426xx::put_(uint8_t reg, uint8_t value) { return this->write_byte(reg, value); }

bool ICM426xx::detect_() {
  uint8_t who = 0;
  if (this->read_register(REG_WHO_AM_I, &who, 1) != i2c::ERROR_OK) {
    ESP_LOGW(TAG, "No motion sensor at 0x%02X", this->address_);
    return false;
  }
  if (who != WHO_AM_I_ICM42627) {
    ESP_LOGW(TAG, "Motion sensor with ID 0x%02X isn't supported yet", who);
    return false;
  }
  return true;
}

bool ICM426xx::woke_by_motion() {
  uint8_t who = 0, status = 0;
  if (this->read_register(REG_WHO_AM_I, &who, 1) != i2c::ERROR_OK || who != WHO_AM_I_ICM42627)
    return false;
  if (this->read_register(REG_INT_STATUS2, &status, 1) != i2c::ERROR_OK)
    return false;
  this->woke_moving_ = (status & 0x07) != 0;
  return this->woke_moving_;
}

void ICM426xx::setup() {
  this->fifo_ = std::make_unique<uint8_t[]>(FIFO_READ);  // NOLINT(cppcoreguidelines-avoid-c-arrays)
  this->present_ = this->detect_();
  if (!this->present_) {
    // Without it, trips start on GPS alone and USB power never sleeps.
    this->disable_loop();
    return;
  }
  this->analyzer_.configure(RATE_HZ, this->motion_g_, this->impact_g_);
  if (!this->stream_()) {
    ESP_LOGW(TAG, "Couldn't set the motion sensor up");
    this->present_ = false;
    this->disable_loop();
    return;
  }
#ifdef USE_BINARY_SENSOR
  if (this->motion_sensor_ != nullptr)
    this->motion_sensor_->publish_initial_state(false);
#endif
  if (this->woke_moving_)
    this->moved_(now_ms());
}

bool ICM426xx::stream_() {
  bool ok = this->put_(REG_BANK_SEL, 0x00);
  ok &= this->put_(REG_SMD_CONFIG, 0x00);  // wake-on-motion off
  ok &= this->put_(REG_INT_SOURCE1, 0x00);
  ok &= this->put_(REG_ACCEL_CONFIG0, 0x08);       // +-16 g, 100 Hz
  ok &= this->put_(REG_GYRO_ACCEL_CONFIG0, 0x01);  // accelerometer filter ODR/2: short impacts pass
  ok &= this->put_(REG_FIFO_CONFIG1, 0x01);        // accelerometer samples into the FIFO
  ok &= this->put_(REG_FIFO_CONFIG, 0x40);         // stream into the FIFO
  ok &= this->put_(REG_PWR_MGMT0, 0x03);           // accelerometer low-noise mode, gyro off, temperature on
  delayMicroseconds(250);                          // the datasheet's wait after a power mode change
  ok &= this->put_(REG_SIGNAL_PATH_RESET, 0x02);   // flush what came before
  this->analyzer_.reset();
  this->settle_ = SETTLE_SAMPLES;
  this->streaming_ = ok;
  this->wake_state_ = WakeState::NONE;
  return ok;
}

void ICM426xx::dump_config() {
  ESP_LOGCONFIG(TAG, "ICM-426xx motion sensor:");
  LOG_I2C_DEVICE(this);
  if (!this->present_) {
    ESP_LOGCONFIG(TAG, "  Not found or not supported: trips start on GPS alone");
    return;
  }
  ESP_LOGCONFIG(TAG,
                "  Model: ICM-42627\n"
                "  Movement: %.2f g between samples, on for %" PRIu32 " s after\n"
                "  Impact: %.1f g beyond gravity",
                this->motion_g_, this->motion_hold_ms_ / 1000, this->impact_g_);
#ifdef USE_BINARY_SENSOR
  LOG_BINARY_SENSOR("  ", "Motion", this->motion_sensor_);
#endif
#ifdef USE_SENSOR
  LOG_SENSOR("  ", "Temperature", this->temperature_sensor_);
  LOG_SENSOR("  ", "Impact", this->impact_sensor_);
#endif
}

// Milliseconds from then to now; 0 when then is later. A stored time is
// ORed with 1 to tell it from "never", so it can be 1 ms past a now that is even.
static uint32_t since(uint32_t now, uint32_t then) { return static_cast<int32_t>(now - then) > 0 ? now - then : 0; }

void ICM426xx::moved_(uint32_t now) {
  if (this->last_motion_ms_ == 0 || since(now, this->last_motion_ms_) >= 60000)
    ESP_LOGV(TAG, "Moved");
  this->last_motion_ms_ = now | 1;
#ifdef USE_BINARY_SENSOR
  if (this->motion_sensor_ != nullptr)
    this->motion_sensor_->publish_state(true);
#endif
}

void ICM426xx::loop() {
  uint32_t now = now_ms();
#ifdef USE_BINARY_SENSOR
  if (this->motion_sensor_ != nullptr && this->motion_sensor_->state &&
      since(now, this->last_motion_ms_) >= this->motion_hold_ms_)
    this->motion_sensor_->publish_state(false);
#endif
  if (!this->streaming_ || now - this->last_read_ms_ < 50)
    return;
  this->last_read_ms_ = now;
  this->read_fifo_();
}

void ICM426xx::read_fifo_() {
  uint8_t count_bytes[2];
  if (this->read_register(REG_FIFO_COUNTH, count_bytes, 2) != i2c::ERROR_OK)
    return;
  size_t count = (static_cast<size_t>(count_bytes[0]) << 8) | count_bytes[1];  // bytes, big-endian
  count = std::min(count - count % PACKET, FIFO_READ);
  if (count == 0)
    return;
  if (this->read_register(REG_FIFO_DATA, this->fifo_.get(), count) != i2c::ERROR_OK)
    return;
  uint32_t now = now_ms();
  for (size_t i = 0; i < count; i += PACKET) {
    const uint8_t *p = this->fifo_.get() + i;
    if ((p[0] & 0xC0) != 0x40) {
      // Not an accelerometer packet (empty, or out of step): start afresh.
      if (++this->fifo_errors_ == 1 || this->fifo_errors_ % 100 == 0)
        ESP_LOGW(TAG, "Unexpected FIFO header 0x%02X; flushing", p[0]);
      this->put_(REG_SIGNAL_PATH_RESET, 0x02);
      this->analyzer_.reset();
      return;
    }
    if (this->settle_ > 0) {
      this->settle_--;
      continue;
    }
    motion::Sample s;
    s.x = static_cast<float>(static_cast<int16_t>((p[1] << 8) | p[2])) * G_PER_LSB;
    s.y = static_cast<float>(static_cast<int16_t>((p[3] << 8) | p[4])) * G_PER_LSB;
    s.z = static_cast<float>(static_cast<int16_t>((p[5] << 8) | p[6])) * G_PER_LSB;
    motion::Result r = this->analyzer_.add(s);
    if (r.moved)
      this->moved_(now);
    if (r.impact_g > 0) {
      ESP_LOGW(TAG, "Impact: %.1f g", r.impact_g);
#ifdef USE_SENSOR
      if (this->impact_sensor_ != nullptr)
        this->impact_sensor_->publish_state(r.impact_g);
#endif
    }
  }
}

void ICM426xx::update() {
#ifdef USE_SENSOR
  if (!this->present_ || this->temperature_sensor_ == nullptr || this->wake_state_ != WakeState::NONE)
    return;
  uint8_t raw[2];
  if (this->read_register(REG_TEMP_DATA1, raw, 2) != i2c::ERROR_OK)
    return;
  int16_t t = static_cast<int16_t>((raw[0] << 8) | raw[1]);
  this->temperature_sensor_->publish_state(static_cast<float>(t) / 132.48f + 25.0f);
#endif
}

void ICM426xx::arm_wake(float threshold_g) {
  if (!this->present_) {
    this->wake_state_ = WakeState::FAILED;
    return;
  }
  this->streaming_ = false;
  this->wake_state_ = WakeState::ARMING;
  bool ok = this->put_(REG_BANK_SEL, 0x00);
  ok &= this->put_(REG_FIFO_CONFIG, 0x00);  // FIFO bypassed
  if (threshold_g <= 0) {
    ok &= this->put_(REG_SMD_CONFIG, 0x00);
    ok &= this->put_(REG_PWR_MGMT0, 0x00);  // all off
    this->wake_state_ = ok ? WakeState::ARMED : WakeState::FAILED;
    return;
  }
  // Wake-on-motion thresholds are in 1/256 g whatever the range, up to 1 g.
  const auto threshold = static_cast<uint8_t>(std::clamp(std::lround(threshold_g * 256.0f), 1L, 255L));
  ok &= this->put_(REG_ACCEL_CONFIG0, 0x69);  // +-2 g, 50 Hz
  ok &= this->put_(REG_PWR_MGMT0, 0x02);      // accelerometer low power, gyro off
  delay(2);                                   // NOLINT: a one-off before sleep
  ok &= this->put_(REG_BANK_SEL, 0x04);
  ok &= this->put_(REG_ACCEL_WOM_X_THR, threshold);
  ok &= this->put_(REG_ACCEL_WOM_X_THR + 1, threshold);
  ok &= this->put_(REG_ACCEL_WOM_X_THR + 2, threshold);
  ok &= this->put_(REG_BANK_SEL, 0x00);
  ok &= this->put_(REG_INT_SOURCE1, 0x07);  // wake-on-motion on all axes
  if (!ok) {
    ESP_LOGW(TAG, "Couldn't set wake-on-motion up");
    this->wake_state_ = WakeState::FAILED;
    return;
  }
  // Wake-on-motion goes on a moment later, compared with the previous sample;
  // its first comparisons can set the flag, so it's cleared once they've passed.
  this->set_timeout("arm", 50, [this, threshold_g]() {
    if (!this->put_(REG_SMD_CONFIG, 0x05)) {
      this->wake_state_ = WakeState::FAILED;
      return;
    }
    this->set_timeout("arm", 200, [this, threshold_g]() {
      uint8_t status;
      this->read_register(REG_INT_STATUS2, &status, 1);
      ESP_LOGD(TAG, "Wake on motion above %.2f g", threshold_g);
      this->wake_state_ = WakeState::ARMED;
    });
  });
}

}  // namespace esphome::icm426xx
