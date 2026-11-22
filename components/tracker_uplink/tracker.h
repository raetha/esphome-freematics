#pragma once
// Decides which GNSS fixes an uplink records, to keep cellular data down while
// keeping the track accurate: a record every few seconds while moving, an
// extra one on a turn, the trip's start and stop (decided once per car by
// trip.h), and an occasional heartbeat while parked. No ESPHome dependencies
// (tests/cpp).

#include <cmath>
#include <cstdint>

#include "../trip_tracker/trip.h"

namespace trackcodec {

struct SampleConfig {
  uint32_t moving_interval_s = 10;   // a record at least this often while moving
  float turn_angle_deg = 30;         // course change that records a fix early
  uint32_t min_turn_interval_s = 3;  // but no closer together than this
  uint32_t heartbeat_s = 900;        // while parked; 0: none (telelogger: 900 s)
};

enum class Decision : uint8_t { NONE, RECORD, START, STOP, HEARTBEAT };

class Sampler {
 public:
  explicit Sampler(const SampleConfig &config = SampleConfig()) : config_(config) {}
  void set_config(const SampleConfig &config) { this->config_ = config; }
  const SampleConfig &config() const { return this->config_; }

  /// A fix at time now, with what the trip detector made of it (trip), whether
  /// the car is on a trip after it (moving) and whether the fix was at moving
  /// speed (fast). Returns whether to record it, and why.
  Decision on_fix(uint32_t now, float course_deg, Trip trip, bool moving, bool fast) {
    this->last_fix_ = now;
    this->has_fix_ = true;
    if (trip == Trip::START)
      return this->recorded_(now, course_deg, Decision::START);
    if (trip == Trip::STOP)
      return this->recorded_(now, course_deg, Decision::STOP);
    if (!moving) {
      if (this->config_.heartbeat_s &&
          (!this->has_record_ || elapsed(now, this->last_record_) >= this->config_.heartbeat_s))
        return this->recorded_(now, course_deg, Decision::HEARTBEAT);
      return Decision::NONE;
    }
    uint32_t since = elapsed(now, this->last_record_);
    if (since >= this->config_.moving_interval_s)
      return this->recorded_(now, course_deg, Decision::RECORD);
    if (fast && since >= this->config_.min_turn_interval_s &&
        angle_between(course_deg, this->last_course_) >= this->config_.turn_angle_deg)
      return this->recorded_(now, course_deg, Decision::RECORD);
    return Decision::NONE;
  }

  /// Time passing without a fix (no signal, GNSS off). A heartbeat without a
  /// location shows the server the device is alive while parked.
  Decision on_tick(uint32_t now, bool moving) {
    if (moving || this->config_.heartbeat_s == 0)
      return Decision::NONE;
    if (this->has_fix_ && elapsed(now, this->last_fix_) < 10)
      return Decision::NONE;  // fixes are flowing
    if (!this->has_record_ || elapsed(now, this->last_record_) >= this->config_.heartbeat_s) {
      this->last_record_ = now;
      this->has_record_ = true;
      return Decision::HEARTBEAT;
    }
    return Decision::NONE;
  }

  /// Something else was recorded now (an impact, a note before sleeping):
  /// the next heartbeat counts from here.
  void recorded(uint32_t now) {
    this->last_record_ = now;
    this->has_record_ = true;
  }

  static float angle_between(float a, float b) {
    float d = std::fabs(a - b);
    while (d >= 360)
      d -= 360;
    return d > 180 ? 360 - d : d;
  }

 protected:
  Decision recorded_(uint32_t now, float course, Decision why) {
    this->recorded(now);
    this->last_course_ = course;
    return why;
  }

  SampleConfig config_;
  bool has_fix_{false};
  bool has_record_{false};
  uint32_t last_fix_{0};
  uint32_t last_record_{0};
  float last_course_{0};
};

/// One car's trips and one uplink's sampling together, as the host tests and
/// the data simulation use them.
struct TrackerConfig : TripConfig, SampleConfig {};

class Tracker {
 public:
  explicit Tracker(const TrackerConfig &config = TrackerConfig()) : trip_(config), sampler_(config) {}
  bool moving() const { return this->trip_.moving(); }
  void set_can_start(bool can) { this->trip_.set_can_start(can); }
  Decision on_fix(uint32_t now, float speed_kmh, float course_deg, float latitude = NAN, float longitude = NAN) {
    Trip trip = this->trip_.on_fix(now, speed_kmh, latitude, longitude);
    return this->sampler_.on_fix(now, course_deg, trip, this->trip_.moving(), this->trip_.fast(speed_kmh));
  }
  Decision on_tick(uint32_t now) { return this->sampler_.on_tick(now, this->trip_.moving()); }

 protected:
  TripDetector trip_;
  Sampler sampler_;
};

}  // namespace trackcodec
