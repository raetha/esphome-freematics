#pragma once
// Decides when a trip starts and stops from GNSS fixes, once per car: every
// uplink reports the same trips. No ESPHome dependencies: tests/cpp builds it
// on the host.

#include <cmath>
#include <cstdint>
#include <numbers>

namespace trackcodec {

struct TripConfig {
  float moving_speed_kmh = 6;   // at or above: moving
  uint32_t stop_delay_s = 180;  // below moving speed this long: the trip is over (telelogger: 180 s)
  // A trip starts only after this many fixes in a row at moving speed, and
  // this far from where the car was parked: a still receiver with a weak fix
  // (indoors, few satellites) reports speeds above walking pace for a fix or
  // two, and drifts tens of metres.
  uint8_t start_fixes = 3;
  float start_distance_m = 50;
  uint32_t reanchor_s = 900;  // while parked, the parked position is taken again this often
};

enum class Trip : uint8_t { NONE, START, STOP };

/// When a trip GNSS has just confirmed began (Unix seconds). GNSS decides
/// whether the car is on a trip; when it began is earlier than its confirming
/// fix: at the first fix of the run at moving speed, or earlier still when the
/// motion sensor had been feeling movement without a pause since before that
/// fix (motion_start, 0: no such run), as when the receiver is still finding
/// its first fix while the car drives off. max_back_s bounds how far before
/// the first fast fix the motion sensor may take the start: loading the car
/// just before driving off is not the trip.
inline uint32_t trip_start_time(uint32_t first_fast, uint32_t motion_start, uint32_t max_back_s) {
  if (motion_start == 0 || motion_start >= first_fast)
    return first_fast;
  uint32_t earliest = first_fast > max_back_s ? first_fast - max_back_s : 0;
  return motion_start > earliest ? motion_start : earliest;
}

/// Seconds from then to now; 0 when the clock has stepped back past then (a
/// clock corrected after waking from deep sleep).
inline uint32_t elapsed(uint32_t now, uint32_t then) { return now > then ? now - then : 0; }

class TripDetector {
 public:
  explicit TripDetector(const TripConfig &config = TripConfig()) : config_(config) {}
  void set_config(const TripConfig &config) { this->config_ = config; }
  const TripConfig &config() const { return this->config_; }

  bool moving() const { return this->moving_; }
  /// Fixes in a row at moving speed while parked (0 on a trip): 1 at a run's first.
  uint8_t run_length() const { return this->fast_fixes_; }
  /// Whether a fix at this speed counts as moving.
  bool fast(float speed_kmh) const { return speed_kmh >= this->config_.moving_speed_kmh; }

  /// Whether a trip may start now: false while the car sits still, so GPS
  /// drift in a garage or a house never starts one. A trip under way isn't affected.
  void set_can_start(bool can) { this->can_start_ = can; }

  /// Carry on a trip that was under way before a restart: the stop delay
  /// counts from the first fix after it, so a car still parked gets its stop.
  void resume() {
    this->moving_ = true;
    this->fast_fixes_ = 0;
    this->resume_pending_ = true;
  }
  /// End a resumed trip that no fix came for: there is nothing to stop it at.
  void abandon() {
    this->moving_ = false;
    this->resume_pending_ = false;
  }

  /// A valid fix at time now (Unix seconds). Without a position (NaN), only
  /// the speed decides a trip's start.
  Trip on_fix(uint32_t now, float speed_kmh, float latitude = NAN, float longitude = NAN) {
    bool is_fast = this->fast(speed_kmh);
    if (this->resume_pending_) {
      this->resume_pending_ = false;
      this->last_fast_ = now;
    }
    if (is_fast)
      this->last_fast_ = now;
    if (!this->moving_) {
      if (!this->has_anchor_ || elapsed(now, this->anchored_at_) >= this->config_.reanchor_s)
        this->anchor_(now, latitude, longitude);
      this->fast_fixes_ = is_fast && this->can_start_ ? this->fast_fixes_ + 1 : 0;
      if (this->fast_fixes_ >= this->config_.start_fixes &&
          this->distance_m_(latitude, longitude) >= this->config_.start_distance_m) {
        this->moving_ = true;
        this->fast_fixes_ = 0;
        return Trip::START;
      }
      return Trip::NONE;
    }
    if (!is_fast && elapsed(now, this->last_fast_) >= this->config_.stop_delay_s) {
      this->moving_ = false;
      this->anchor_(now, latitude, longitude);
      return Trip::STOP;
    }
    return Trip::NONE;
  }

 protected:
  // Where the car is parked, for the start distance.
  void anchor_(uint32_t now, float latitude, float longitude) {
    this->anchor_lat_ = latitude;
    this->anchor_lon_ = longitude;
    this->anchored_at_ = now;
    this->has_anchor_ = !std::isnan(latitude) && !std::isnan(longitude);
  }

  // Metres from the parked position (equirectangular, plenty for tens of
  // metres); infinite when either position is unknown, so speed alone decides.
  float distance_m_(float latitude, float longitude) const {
    if (!this->has_anchor_ || std::isnan(latitude) || std::isnan(longitude))
      return INFINITY;
    constexpr float deg_to_m = 111195.0f;  // metres per degree of latitude
    float dlat = (latitude - this->anchor_lat_) * deg_to_m;
    float dlon = (longitude - this->anchor_lon_) * deg_to_m * std::cos(latitude * std::numbers::pi_v<float> / 180.0f);
    return std::sqrt(dlat * dlat + dlon * dlon);
  }

  TripConfig config_;
  bool moving_{false};
  bool can_start_{true};
  bool resume_pending_{false};
  uint8_t fast_fixes_{0};
  uint32_t last_fast_{0};
  bool has_anchor_{false};
  uint32_t anchored_at_{0};
  float anchor_lat_{0};
  float anchor_lon_{0};
};

}  // namespace trackcodec
