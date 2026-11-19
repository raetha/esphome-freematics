#pragma once
// Decides which GNSS fixes become records, to keep cellular data down while
// keeping the track accurate: a record every few seconds while moving, an
// extra one on a turn, one when a trip starts and when it stops, and an
// occasional heartbeat while parked. No ESPHome dependencies (tests/cpp).

#include <cstdint>
#include <cmath>

namespace trackcodec {

struct TrackerConfig {
  float moving_speed_kmh = 6;        // at or above: moving
  uint32_t stop_delay_s = 180;       // below moving_speed this long: trip over (telelogger: 180 s)
  uint32_t moving_interval_s = 10;   // a record at least this often while moving
  float turn_angle_deg = 30;         // course change that records a fix early
  uint32_t min_turn_interval_s = 3;  // but no closer together than this
  uint32_t heartbeat_s = 900;        // while parked; 0: none (telelogger: 900 s)
};

enum class Decision : uint8_t { NONE, RECORD, START, STOP, HEARTBEAT };

class Tracker {
 public:
  explicit Tracker(const TrackerConfig &config = TrackerConfig()) : config_(config) {}
  void set_config(const TrackerConfig &config) { this->config_ = config; }
  const TrackerConfig &config() const { return this->config_; }

  bool moving() const { return this->moving_; }

  // A valid fix at time now (Unix seconds). Returns whether to record it, and why.
  Decision on_fix(uint32_t now, float speed_kmh, float course_deg) {
    this->last_fix_ = now;
    this->has_fix_ = true;
    bool fast = speed_kmh >= this->config_.moving_speed_kmh;
    if (fast) this->last_fast_ = now;

    if (!this->moving_) {
      if (fast) {
        this->moving_ = true;
        return this->recorded_(now, course_deg, Decision::START);
      }
      if (this->config_.heartbeat_s && (!this->has_record_ || now - this->last_record_ >= this->config_.heartbeat_s))
        return this->recorded_(now, course_deg, Decision::HEARTBEAT);
      return Decision::NONE;
    }

    if (!fast && now - this->last_fast_ >= this->config_.stop_delay_s) {
      this->moving_ = false;
      return this->recorded_(now, course_deg, Decision::STOP);
    }
    uint32_t since = now - this->last_record_;
    if (since >= this->config_.moving_interval_s)
      return this->recorded_(now, course_deg, Decision::RECORD);
    if (fast && since >= this->config_.min_turn_interval_s &&
        angle_between(course_deg, this->last_course_) >= this->config_.turn_angle_deg)
      return this->recorded_(now, course_deg, Decision::RECORD);
    return Decision::NONE;
  }

  // Time passing without a fix (no signal, GNSS off). A heartbeat without a
  // location keeps the device alive in Traccar while parked.
  Decision on_tick(uint32_t now) {
    if (this->moving_ || this->config_.heartbeat_s == 0) return Decision::NONE;
    if (this->has_fix_ && now - this->last_fix_ < 10) return Decision::NONE;  // fixes are flowing
    if (!this->has_record_ || now - this->last_record_ >= this->config_.heartbeat_s) {
      this->last_record_ = now;
      this->has_record_ = true;
      return Decision::HEARTBEAT;
    }
    return Decision::NONE;
  }

  static float angle_between(float a, float b) {
    float d = std::fabs(a - b);
    while (d >= 360) d -= 360;
    return d > 180 ? 360 - d : d;
  }

 protected:
  Decision recorded_(uint32_t now, float course, Decision why) {
    this->last_record_ = now;
    this->has_record_ = true;
    this->last_course_ = course;
    return why;
  }

  TrackerConfig config_;
  bool moving_{false};
  bool has_fix_{false};
  bool has_record_{false};
  uint32_t last_fix_{0};
  uint32_t last_fast_{0};
  uint32_t last_record_{0};
  float last_course_{0};
};

}  // namespace trackcodec
