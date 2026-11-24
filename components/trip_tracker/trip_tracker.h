#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_TIME
#include "esphome/components/time/real_time_clock.h"
#endif

#include "trip.h"

namespace esphome::trip_tracker {

using ::trackcodec::Trip;

/// A GNSS fix, as the trip tracker passes it on.
struct Fix {
  uint32_t time;  // Unix seconds, UTC
  float latitude;
  float longitude;
  float speed_kmh;
  float course_deg;
  float altitude_m;  // NAN: unknown
  uint8_t satellites;
  uint8_t hdop;  // x 10
};

/// What rides along with trip starts, stops, heartbeats and notes.
struct Extras {
  uint16_t battery_mv{0};  // 0: unknown
  bool has_temperature{false};
  int8_t temperature_c{0};
  int8_t signal_dbm{0};  // 0: unknown (cellular off)
};

/// How a trip GNSS has just confirmed began (trackcodec::trip_start_time).
struct TripStart {
  uint32_t time{0};          // Unix seconds
  bool at_first_fix{false};  // it began at first; otherwise earlier, still where the car was parked
  Fix first{};               // the run's first fix at moving speed
};

/// An uplink (tracker_uplink) that reports this car's trips.
class Listener {
 public:
  /// A fix, with what the trip detector made of it; start is set only when
  /// trip is START.
  virtual void on_trip_fix(const Fix &fix, Trip trip, bool moving, bool fast, const TripStart *start) = 0;
  /// Something to report now, out of the usual sampling: an impact (peak in
  /// tenths of g), or that the device goes to sleep and reports again within
  /// next_report_min minutes.
  virtual void on_trip_note(uint32_t time, uint8_t impact_dg, uint16_t next_report_min) = 0;
  virtual size_t queued() const = 0;
  virtual bool delivered_any() const = 0;
  virtual void flush() = 0;
};

/// Decides once per car when a trip starts and stops (trip.h), from GNSS fixes
/// and the motion sensor, and hands each fix to the uplinks, which decide what
/// to report. A trip starts only soon after the motion sensor felt the car
/// move, so GPS drift in a garage never starts one.
class TripTracker : public Component {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_config(const ::trackcodec::TripConfig &config) { this->detector_.set_config(config); }
  void set_start_motion(uint32_t ms) { this->start_motion_ms_ = ms; }
#ifdef USE_BINARY_SENSOR
  void set_motion(binary_sensor::BinarySensor *s) { this->motion_ = s; }
#endif
#ifdef USE_SENSOR
  void set_battery(sensor::Sensor *s) { this->battery_ = s; }
  void set_temperature(sensor::Sensor *s) { this->temperature_ = s; }
  void set_signal(sensor::Sensor *s) { this->signal_ = s; }
  void set_impact(sensor::Sensor *s) { this->impact_ = s; }
#endif
#ifdef USE_TIME
  void set_time(time::RealTimeClock *t) { this->time_ = t; }
#endif
  void set_model(const char *model) { this->model_ = model; }
  void set_firmware(const char *firmware) { this->firmware_ = firmware; }
  /// The device's model and firmware version ("" when not set).
  const char *model() const { return this->model_; }
  const char *firmware() const { return this->firmware_; }
  void add_listener(Listener *l) { this->listeners_.push_back(l); }

  /// A valid GNSS fix from any source with these members (a GNSS
  /// component's own fix type): epoch (Unix seconds; 0 when unknown), latitude, longitude,
  /// speed_kmh, course_deg, altitude_m, satellites, hdop.
  template<typename F> void add_fix(const F &f) {
    Fix fix{f.epoch,
            f.latitude,
            f.longitude,
            std::isnan(f.speed_kmh) ? 0.0f : f.speed_kmh,
            std::isnan(f.course_deg) ? 0.0f : f.course_deg,
            f.altitude_m,
            f.satellites,
            f.hdop};
    this->process_fix_(fix);
  }

  bool is_moving() const { return this->detector_.moving(); }
  /// The time now (Unix seconds), from the last fix or the clock once it has
  /// been set since the start; false when unknown. After deep sleep the clock
  /// runs on, but its slow oscillator drifts by minutes over a night, so it
  /// isn't trusted until SNTP or a fix has set it.
  bool now(uint32_t *unix) const;
  /// Loop time of the last fix; 0: none yet.
  uint32_t last_fix_ms() const { return this->last_fix_ms_; }
  const Fix &last_fix() const { return this->last_fix_; }
  Extras extras() const;
  /// Loop time the motion sensor last felt the car move; 0: not since start.
  uint32_t last_motion_ms() const { return this->last_motion_ms_; }
  /// Whether a motion sensor is reporting.
  bool has_motion_sensor() const;

  /// Records waiting in all uplinks together.
  size_t queued() const;
  /// Whether every uplink has delivered something since the start.
  bool all_delivered() const;
  /// Send what's queued in every uplink at the next chance.
  void flush();
  /// The device goes to sleep and reports again within this many seconds:
  /// each uplink records that (Home Assistant shows the dongle as reporting
  /// until then) and sends it.
  void going_to_sleep(uint32_t next_report_s);

  Trigger<> *get_trip_start_trigger() { return &this->trip_start_trigger_; }
  Trigger<> *get_trip_stop_trigger() { return &this->trip_stop_trigger_; }
  Trigger<Fix> *get_fix_trigger() { return &this->fix_trigger_; }
  Trigger<float> *get_impact_trigger() { return &this->impact_trigger_; }

 protected:
  void process_fix_(Fix fix);
  void on_impact_(float peak_g);
  void note_(uint8_t impact_dg, uint16_t next_report_min);

  ::trackcodec::TripDetector detector_;
  uint32_t start_motion_ms_{300000};
  std::vector<Listener *> listeners_;
#ifdef USE_BINARY_SENSOR
  binary_sensor::BinarySensor *motion_{nullptr};
#endif
#ifdef USE_SENSOR
  sensor::Sensor *battery_{nullptr};
  sensor::Sensor *temperature_{nullptr};
  sensor::Sensor *signal_{nullptr};
  sensor::Sensor *impact_{nullptr};
#endif
#ifdef USE_TIME
  time::RealTimeClock *time_{nullptr};
#endif
  // Whether a trip is under way, kept across restarts: written at each start
  // and stop only.
  ESPPreferenceObject pref_;
  void save_trip_(bool on);
  const char *model_{""};
  const char *firmware_{""};
  Fix last_fix_{};
  uint32_t last_fix_ms_{0};
  uint32_t last_motion_ms_{0};
  // The motion sensor's current run of movement: when it began (0: none now).
  // The sensor stays on a few seconds after the last movement, so a pause
  // longer than that (loading the car, then a wait) ends a run.
  uint32_t motion_run_ms_{0};
  // The run of fixes at moving speed that may become a trip: its first fix,
  // and when the motion sensor's run under way at that fix began (0: none).
  Fix run_first_{};
  uint32_t run_motion_start_{0};
  uint8_t clock_syncs_needed_{1};  // until 0, the clock isn't trusted
  Trigger<> trip_start_trigger_;
  Trigger<> trip_stop_trigger_;
  Trigger<Fix> fix_trigger_;
  Trigger<float> impact_trigger_;
};

template<typename... Ts> class TripMovingCondition : public Condition<Ts...>, public Parented<TripTracker> {
 public:
  bool check(const Ts &...x) override { return this->parent_->is_moving(); }
};

}  // namespace esphome::trip_tracker
