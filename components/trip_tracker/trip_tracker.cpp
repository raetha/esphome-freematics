#include "trip_tracker.h"

#include <algorithm>

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"

namespace esphome::trip_tracker {

static const char *const TAG = "trip_tracker";

static uint32_t now_ms() { return App.get_loop_component_start_time(); }

void TripTracker::setup() {
  // A trip under way when the device restarted (unplugged, a crash, an
  // update) goes on: once the car has been still for the stop delay after the
  // first fix, its stop is reported as usual. Without that, the stop would
  // never come.
  this->pref_ = global_preferences->make_preference<uint8_t>(fnv1_hash("trip_tracker"));
  uint8_t on = 0;
  if (this->pref_.load(&on) && on == 1) {
    this->detector_.resume();
    uint32_t delay_s = this->detector_.config().stop_delay_s;
    ESP_LOGI(TAG, "A trip was under way before the restart: it goes on until the car has been still for %" PRIu32 " s",
             delay_s);
    // No fix at all (a garage) leaves nowhere to stop it: it ends unreported.
    this->set_timeout("resume", delay_s * 1000, [this]() {
      if (this->last_fix_ms_ == 0 && this->detector_.moving()) {
        ESP_LOGW(TAG, "No GPS fix since the restart: the trip from before it ends without a stop report");
        this->detector_.abandon();
        this->save_trip_(false);
      }
    });
  }
#ifdef USE_BINARY_SENSOR
  if (this->motion_ != nullptr) {
    // On while the car is being moved: both edges mark movement (off comes a
    // few seconds after the last).
    this->motion_->add_on_state_callback([this](bool on) { this->last_motion_ms_ = now_ms() | 1; });
    if (this->motion_->has_state() && this->motion_->state)
      this->last_motion_ms_ = now_ms() | 1;
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
  if (this->model_[0] != '\0' || this->firmware_[0] != '\0')
    ESP_LOGCONFIG(TAG, "  Device: %s, firmware %s", this->model_, this->firmware_);
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
  this->fix_trigger_.trigger(fix);
  for (auto *l : this->listeners_)
    l->on_trip_fix(fix, trip, moving, fast);
  if (trip == Trip::START) {
    ESP_LOGI(TAG, "Trip started");
    this->save_trip_(true);
    this->trip_start_trigger_.trigger();
  } else if (trip == Trip::STOP) {
    ESP_LOGI(TAG, "Trip stopped");
    this->save_trip_(false);
    this->trip_stop_trigger_.trigger();
  }
}

void TripTracker::save_trip_(bool on) {
  uint8_t v = on ? 1 : 0;
  this->pref_.save(&v);
  // Written at once (twice a trip): a restart soon after must still find it.
  global_preferences->sync();
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
