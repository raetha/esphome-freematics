#include "trip_tracker.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"

namespace esphome::trip_tracker {

static const char *const TAG = "trip_tracker";

static uint32_t now_ms() { return App.get_loop_component_start_time(); }

void TripTracker::setup() {
  // A trip under way when the device restarted (unplugged, a power cut, a
  // crash, an update) either went on through the restart or ended before it,
  // the car parked and then the power cut: the first fix after it tells
  // (settle_trip_). Until then it counts as under way, so no heartbeat says
  // otherwise.
  this->pref_ = global_preferences->make_preference<SavedTrip>(fnv1_hash("trip_tracker_saved"));
  if (this->pref_.load(&this->trip_) && this->trip_.on && this->trip_.last_fast != 0) {
    this->trip_unsettled_ = true;
    this->detector_.resume(this->trip_.last_fast);
    ESP_LOGI(
        TAG,
        "A trip was under way before the restart: it goes on if the car is moving again, or ends where it last was");
    this->set_timeout("resume", this->detector_.config().stop_delay_s * 1000,
                      [this]() { this->settle_without_fix_(); });
  } else {
    this->trip_ = SavedTrip{};
  }
  // While parked and awake, what the start rule sees, once a minute in the
  // debug log: a drive that never became a trip can be explained afterwards.
#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_DEBUG
  this->set_interval("status", 60000, [this]() { this->log_parked_status_(); });
#endif
#ifdef USE_BINARY_SENSOR
  if (this->motion_ != nullptr) {
    // On while the car is being moved: both edges mark movement (off comes a
    // few seconds after the last).
    this->motion_->add_on_state_callback([this](bool on) {
      this->last_motion_ms_ = now_ms() | 1;
      if (!on) {
        this->motion_run_ms_ = 0;
      } else if (this->motion_run_ms_ == 0) {
        this->motion_run_ms_ = now_ms() | 1;
      }
    });
    if (this->motion_->has_state() && this->motion_->state)
      this->last_motion_ms_ = this->motion_run_ms_ = now_ms() | 1;
  }
#endif
#ifdef USE_SENSOR
  if (this->impact_ != nullptr)
    this->impact_->add_on_state_callback([this](float g) { this->on_impact_(g); });
#endif
#ifdef USE_TIME
  if (this->time_ != nullptr) {
    // A clock already set at the start ran on through deep sleep or a
    // restart: the time component reports it as synchronized at once, so it
    // takes a second report (SNTP's own) to trust it.
    this->clock_syncs_needed_ = this->time_->now().is_valid() ? 2 : 1;
    this->time_->add_on_time_sync_callback([this]() {
      if (this->clock_syncs_needed_ > 0)
        this->clock_syncs_needed_--;
    });
  }
#endif
}

void TripTracker::dump_config() {
  const auto &c = this->detector_.config();
  ESP_LOGCONFIG(TAG,
                "Trip tracker:\n"
                "  Moving at %.0f km/h; a trip starts after %u fixes in a row, %.0f m from where it was parked\n"
                "  Stopped after %" PRIu32 " s below that\n"
                "  Uplinks: %u",
                c.moving_speed_kmh, c.start_fixes, c.start_distance_m, c.stop_delay_s,
                (unsigned) this->listeners_.size());
  if (this->start_motion_ms_ == 0) {
    ESP_LOGCONFIG(TAG, "  Trips start on GPS alone");
  } else {
    ESP_LOGCONFIG(TAG, "  A trip starts only within %" PRIu32 " s of the motion sensor feeling the car move%s",
                  this->start_motion_ms_ / 1000, this->has_motion_sensor() ? "" : " (no motion sensor: GPS alone)");
  }
  if (this->model_[0] != '\0' || this->firmware_[0] != '\0') {
    ESP_LOGCONFIG(TAG, "  Device: %s, firmware %s", this->model_, this->firmware_);
  }
  ESP_LOGCONFIG(TAG, "  On a trip: %s", YESNO(this->is_moving()));
}

bool TripTracker::has_motion_sensor() const {
#ifdef USE_BINARY_SENSOR
  return this->motion_ != nullptr && this->motion_->has_state();
#else
  return false;
#endif
}

bool TripTracker::now(uint32_t *unix) const {
  if (this->last_fix_ms_ != 0 && this->last_fix_.time != 0) {
    *unix = this->last_fix_.time + (now_ms() - this->last_fix_ms_) / 1000;
    return true;
  }
#ifdef USE_TIME
  if (this->time_ != nullptr && this->clock_syncs_needed_ == 0) {
    auto t = this->time_->now();
    if (t.is_valid()) {
      *unix = t.timestamp;
      return true;
    }
  }
#endif
  return false;
}

Extras TripTracker::extras() const {
  Extras e;
#ifdef USE_SENSOR
  if (this->battery_ != nullptr && this->battery_->has_state() && !std::isnan(this->battery_->state))
    e.battery_mv = static_cast<uint16_t>(std::clamp(this->battery_->state * 1000.0f, 0.0f, 65535.0f));
  if (this->temperature_ != nullptr && this->temperature_->has_state() && !std::isnan(this->temperature_->state)) {
    e.has_temperature = true;
    e.temperature_c = static_cast<int8_t>(std::clamp(std::lround(this->temperature_->state), -128L, 127L));
  }
  if (this->signal_ != nullptr && this->signal_->has_state() && !std::isnan(this->signal_->state))
    e.signal_dbm = static_cast<int8_t>(std::clamp(std::lround(this->signal_->state), -128L, -1L));
#endif
  return e;
}

void TripTracker::process_fix_(Fix fix) {
  if (fix.time == 0 && !this->now(&fix.time))
    return;  // no time yet: nothing useful to record
  if (this->trip_unsettled_)
    this->settle_trip_(&fix);
  this->last_fix_ = fix;
  this->last_fix_ms_ = now_ms() | 1;
  // GPS drift while the car sits still (a garage, a house, a desk) never
  // starts a trip: the motion sensor must have felt it move lately. Without
  // a motion sensor, GPS decides alone.
  bool can_start =
      this->start_motion_ms_ == 0 || !this->has_motion_sensor() ||
      (this->last_motion_ms_ != 0 &&
       // signed: a stamp ORed with 1 can be 1 ms past now
       static_cast<int32_t>(now_ms() - this->last_motion_ms_) < static_cast<int32_t>(this->start_motion_ms_));
  this->detector_.set_can_start(can_start);
  Trip trip = this->detector_.on_fix(fix.time, fix.speed_kmh, fix.latitude, fix.longitude);
  bool moving = this->detector_.moving();
  bool fast = this->detector_.fast(fix.speed_kmh);
  // The first fix of a run at moving speed: if the run becomes a trip, the
  // trip began there, or when the motion sensor's run of movement still going
  // on at that fix began, if that's earlier. (With start_fixes 1, the fix that
  // starts the trip is its own run.)
  bool run_begins = trip == Trip::START ? this->run_first_.time == 0 : !moving && this->detector_.run_length() == 1;
  if (run_begins) {
    this->run_first_ = fix;
    this->run_motion_start_ = 0;
    if (this->motion_run_ms_ != 0) {
      uint32_t ago_s = (now_ms() - this->motion_run_ms_) / 1000;
      if (ago_s > 0 && ago_s < fix.time)
        this->run_motion_start_ = fix.time - ago_s;
    }
  } else if (!moving && this->detector_.run_length() == 0) {
    this->run_first_ = Fix{};  // the run broke off: no trip from it
  }
  TripStart start;
  if (trip == Trip::START) {
    start.first = this->run_first_;
    start.time =
        ::trackcodec::trip_start_time(this->run_first_.time, this->run_motion_start_, this->start_motion_ms_ / 1000);
    start.at_first_fix = start.time == this->run_first_.time;
    this->run_first_ = Fix{};
    this->run_motion_start_ = 0;
  }
  this->fix_trigger_.trigger(fix);
  for (auto *l : this->listeners_)
    l->on_trip_fix(fix, trip, moving, fast, trip == Trip::START ? &start : nullptr);
  if (trip == Trip::START) {
    ESP_LOGI(TAG, "Trip started %" PRIu32 " s before GPS confirmed it (%s)", fix.time - start.time,
             start.at_first_fix ? "its first fix at moving speed" : "when the motion sensor first felt it");
    this->trip_ = SavedTrip{fix.time, fix.latitude, fix.longitude, fix.satellites, true};
    this->save_trip_(true);
    this->trip_start_trigger_.trigger();
  } else if (trip == Trip::STOP) {
    ESP_LOGI(TAG, "Trip stopped");
    this->trip_ = SavedTrip{};
    this->save_trip_(true);
    this->trip_stop_trigger_.trigger();
  } else if (moving) {
    // Where a restart from now on ends the trip, if the car isn't moving
    // again after it: kept once a minute, at the preferences' own pace
    this->trip_ = SavedTrip{fast ? fix.time : this->trip_.last_fast, fix.latitude, fix.longitude, fix.satellites, true};
    if (now_ms() - this->trip_saved_ms_ >= 60000)
      this->save_trip_(false);
  }
}

void TripTracker::log_parked_status_() {
  if (this->detector_.moving())
    return;
  char motion[48] = "no motion sensor";
  if (this->has_motion_sensor()) {
    if (this->last_motion_ms_ == 0) {
      snprintf(motion, sizeof(motion), "no movement felt");
    } else {
      snprintf(motion, sizeof(motion), "movement felt %" PRIu32 " s ago", (now_ms() - this->last_motion_ms_) / 1000);
    }
  }
  if (this->last_fix_ms_ == 0) {
    ESP_LOGD(TAG, "Parked: no GPS fix yet; %s", motion);
    return;
  }
  const Fix &f = this->last_fix_;
  ESP_LOGD(TAG,
           "Parked: last fix %" PRIu32 " s ago, %.0f km/h, %u satellites, HDOP %.1f, %.0f m from where it's parked, "
           "%u fixes at moving speed in a row; %s",
           (now_ms() - this->last_fix_ms_) / 1000, f.speed_kmh, f.satellites, f.hdop / 10.0f,
           this->detector_.from_parked_m(f.latitude, f.longitude), this->detector_.run_length(), motion);
}

void TripTracker::save_trip_(bool sync) {
  this->pref_.save(&this->trip_);
  this->trip_saved_ms_ = now_ms();
  // A start or stop is written at once: a restart soon after must still find it.
  if (sync)
    global_preferences->sync();
}

void TripTracker::settle_trip_(const Fix *fix) {
  this->trip_unsettled_ = false;
  this->cancel_timeout("resume");
  uint32_t stop_delay_s = this->detector_.config().stop_delay_s;
  if (fix != nullptr && ::trackcodec::trip_went_on(fix->time, this->trip_.last_fast, stop_delay_s)) {
    ESP_LOGI(TAG, "The trip from before the restart goes on");
    return;  // the detector resumed it from its last fast fix
  }
  // The car parked, then the power went: the stop is what the detector would
  // have reported with the power on, the stop delay after the last fix at
  // moving speed, where the car was last seen. Dated then, it follows every
  // record sent before the power went (servers ignore a stop older than the
  // newest position they have).
  Fix stop{};
  stop.time = this->trip_.last_fast + stop_delay_s;
  stop.latitude = this->trip_.latitude;
  stop.longitude = this->trip_.longitude;
  stop.course_deg = 0;
  stop.altitude_m = NAN;
  stop.satellites = this->trip_.satellites;
  ESP_LOGI(TAG, "The trip from before the restart stopped %s: reporting its stop where the car was last seen",
           fix != nullptr ? "before it (the car isn't moving now)" : "before it (no GPS fix since)");
  this->detector_.abandon();
  for (auto *l : this->listeners_)
    l->on_trip_fix(stop, Trip::STOP, false, false, nullptr);
  this->trip_ = SavedTrip{};
  this->save_trip_(true);
  this->trip_stop_trigger_.trigger();
}

void TripTracker::settle_without_fix_() {
  if (!this->trip_unsettled_)
    return;
  uint32_t stop_delay_ms = this->detector_.config().stop_delay_s * 1000;
  // Still felt moving: driving on while the receiver finds its first fix
  // (minutes, at times), so the first fix decides.
  if (this->has_motion_sensor() && this->last_motion_ms_ != 0 && now_ms() - this->last_motion_ms_ < stop_delay_ms) {
    this->set_timeout("resume", stop_delay_ms, [this]() { this->settle_without_fix_(); });
    return;
  }
  // No fix and no movement for the stop delay (a garage): the car hasn't been
  // driven since.
  this->settle_trip_(nullptr);
}

void TripTracker::note_(uint8_t impact_dg, uint16_t next_report_min) {
  uint32_t unix;
  if (!this->now(&unix))
    return;
  for (auto *l : this->listeners_)
    l->on_trip_note(unix, impact_dg, next_report_min);
}

void TripTracker::on_impact_(float peak_g) {
  if (std::isnan(peak_g) || peak_g <= 0)
    return;
  ESP_LOGW(TAG, "Impact of %.1f g: reporting it now", peak_g);
  this->note_(static_cast<uint8_t>(std::clamp(std::lround(peak_g * 10), 1L, 255L)), 0);
  this->impact_trigger_.trigger(peak_g);
}

void TripTracker::going_to_sleep(uint32_t next_report_s) {
  uint32_t minutes = (next_report_s + 59) / 60;
  this->note_(0, static_cast<uint16_t>(std::clamp<uint32_t>(minutes, 1, 65535)));
}

size_t TripTracker::queued() const {
  size_t n = 0;
  for (auto *l : this->listeners_)
    n += l->queued();
  return n;
}

bool TripTracker::all_delivered() const {
  for (auto *l : this->listeners_) {
    if (!l->delivered_any())
      return false;
  }
  return !this->listeners_.empty();
}

void TripTracker::flush() {
  for (auto *l : this->listeners_)
    l->flush();
}

}  // namespace esphome::trip_tracker
