#include "tracker_uplink.h"

#include <algorithm>
#include <cmath>

#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "esphome/components/network/util.h"

namespace esphome::tracker_uplink {

static const char *const TAG = "tracker_uplink";

// Timing uses the loop's start time, as ESPHome asks of components.
static uint32_t now_ms() { return App.get_loop_component_start_time(); }

void TrackerUplink::setup() {
  // Everything that needs memory is allocated here, once.
  this->backlog_.init(this->max_queue_, this->store_);
  if (!this->backlog_.empty())
    ESP_LOGI(TAG, "%u records from before the restart to send", (unsigned) this->backlog_.size());
  this->last_tick_ = now_ms();
  if (!this->transport_->setup(this->batch_max_))
    this->mark_failed();
  this->transport_->set_identity(this->parent_->model(), this->parent_->firmware());
}

void TrackerUplink::dump_config() {
  const auto &c = this->sampler_.config();
  ESP_LOGCONFIG(TAG,
                "Uplink to %s:\n"
                "  Send interval: %" PRIu32 " s\n"
                "  Queue: %u records, up to %u per send\n"
                "  Moving: a record every %" PRIu32 " s or on a %.0f deg turn; parked: a heartbeat every %" PRIu32 " s",
                this->transport_->server(), this->send_interval_ / 1000, this->max_queue_, this->batch_max_,
                c.moving_interval_s, c.turn_angle_deg, c.heartbeat_s);
  this->transport_->dump_config();
#ifdef USE_FILE_QUEUE
  if (this->store_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Records kept on file until delivered (%s)",
                  this->backlog_.stored() ? "ready" : "unavailable");
  }
#endif
  // Shown whenever a log viewer connects, so it says how the uplink is doing now.
  ESP_LOGCONFIG(TAG, "  Waiting: %u records; delivered since boot: %" PRIu32 " (%" PRIu32 " bytes sent)",
                (unsigned) this->queued(), this->records_sent_, this->transport_->bytes_sent());
  if (!this->transport_->acknowledged()) {
    return;
  } else if (this->last_ack_at_ != 0) {
    ESP_LOGCONFIG(TAG, "  Last answer from %s: %" PRIu32 " s ago", this->transport_->server(),
                  (now_ms() - this->last_ack_at_) / 1000);
  } else {
    ESP_LOGCONFIG(TAG, "  Last answer from %s: none since boot", this->transport_->server());
  }
}

Record TrackerUplink::base_record_(uint32_t time, Decision why) const {
  Record r{};
  r.time = time;
  r.kind = static_cast<uint8_t>(why);
  r.motion = -1;
  // Battery, temperature, signal and motion ride along on trip starts, stops,
  // heartbeats and notes only.
  if (why == Decision::START || why == Decision::STOP || why == Decision::HEARTBEAT) {
    r.motion = this->parent_->is_moving() ? 1 : 0;
    trip_tracker::Extras e = this->parent_->extras();
    r.battery_mv = e.battery_mv;
    r.has_temperature = e.has_temperature;
    r.temperature_c = e.temperature_c;
    r.signal_dbm = e.signal_dbm;
  }
  return r;
}

void TrackerUplink::set_location_(Record &r, const trip_tracker::Fix &fix) const {
  r.has_location = true;
  r.latitude = fix.latitude;
  r.longitude = fix.longitude;
  r.speed_kmh = fix.speed_kmh;
  r.course_deg = fix.course_deg;
  r.satellites = fix.satellites;
  r.hdop = fix.hdop;
  if (this->altitude_ && !std::isnan(fix.altitude_m)) {
    r.has_altitude = true;
    r.altitude_m = static_cast<int16_t>(std::lround(std::clamp(fix.altitude_m, -32768.0f, 32767.0f)));
  }
}

void TrackerUplink::on_trip_fix(const trip_tracker::Fix &fix, trip_tracker::Trip trip, bool moving, bool fast,
                                const trip_tracker::TripStart *start) {
  Decision why = this->sampler_.on_fix(fix.time, fix.course_deg, trip, moving, fast);
  if (why == Decision::NONE)
    return;
  if (why == Decision::START && start != nullptr && start->time < fix.time) {
    // The trip began before the fix that confirmed it: the start record is
    // dated then. Begun at the run's first fast fix, it has that fix's
    // position; begun earlier (the motion sensor felt the car move before
    // GNSS saw it), none, as the car was still where it was parked: servers
    // keep the last position they have. The first fast fix, then this one,
    // follow as fixes of the trip.
    Record s = this->base_record_(start->time, Decision::START);
    if (start->at_first_fix)
      this->set_location_(s, start->first);
    this->enqueue_(s);
    if (!start->at_first_fix && start->first.time > start->time && start->first.time < fix.time) {
      Record first = this->base_record_(start->first.time, Decision::RECORD);
      this->set_location_(first, start->first);
      this->enqueue_(first);
    }
    why = Decision::RECORD;
  }
  Record r = this->base_record_(fix.time, why);
  this->set_location_(r, fix);
  this->enqueue_(r);
  // The parking spot and heartbeats go out at once.
  if (why == Decision::STOP || why == Decision::HEARTBEAT)
    this->flush_requested_ = true;
}

void TrackerUplink::on_trip_note(uint32_t time, uint8_t impact_dg, uint16_t next_report_min) {
  bool moving = this->parent_->is_moving();
  // On a trip it's a fix (the trip goes on); parked, a heartbeat.
  Record r = this->base_record_(time, moving ? Decision::RECORD : Decision::HEARTBEAT);
  if (moving) {  // the extras a heartbeat carries
    trip_tracker::Extras e = this->parent_->extras();
    r.battery_mv = e.battery_mv;
    r.has_temperature = e.has_temperature;
    r.temperature_c = e.temperature_c;
    r.signal_dbm = e.signal_dbm;
  }
  r.impact_dg = impact_dg;
  r.next_report_min = next_report_min;
  uint32_t fix_ms = this->parent_->last_fix_ms();
  if (fix_ms != 0 && now_ms() - fix_ms < 10000)
    this->set_location_(r, this->parent_->last_fix());
  this->enqueue_(r);
  this->sampler_.recorded(time);
  this->flush_requested_ = true;
}

void TrackerUplink::enqueue_(const Record &record) {
  switch (this->backlog_.add(record, this->in_flight_)) {
    case ::trackcodec::Added::OK:
      break;
    case ::trackcodec::Added::DROPPED_OLDEST:
      ESP_LOGW(TAG, "Queue full: dropped the oldest unsent record");
      break;
    case ::trackcodec::Added::DROPPED:
      ESP_LOGW(TAG, "Queue full: record dropped");
      break;
  }
  ESP_LOGV(TAG, "Queued a record for %s (%u waiting%s)", this->transport_->server(), (unsigned) this->backlog_.size(),
           this->backlog_.stored() ? ", on file" : "");
}

void TrackerUplink::send_() {
  this->last_send_ = now_ms();
  this->flush_requested_ = false;
  size_t n = this->transport_->send(this->backlog_.memory(), this->batch_max_);
  if (n > 0)
    this->in_flight_ = n;
}

void TrackerUplink::loop() {
  uint32_t now = now_ms();

  // Heartbeats while parked without a fix (the sampler ignores this while fixes flow).
  if (now - this->last_tick_ >= 1000) {
    this->last_tick_ = now;
    uint32_t unix;
    if (this->parent_->now(&unix) && this->sampler_.on_tick(unix, this->parent_->is_moving()) == Decision::HEARTBEAT) {
      this->enqueue_(this->base_record_(unix, Decision::HEARTBEAT));
      this->flush_requested_ = true;
    }
  }

  if (this->in_flight_ > 0) {
    switch (this->transport_->poll(now)) {
      case SendResult::PENDING:
        return;
      case SendResult::ACKED: {
        size_t n = std::min(this->in_flight_, this->backlog_.memory().size());
        this->backlog_.acknowledged(n);
        this->records_sent_ += n;
        if (this->transport_->acknowledged()) {
          this->last_ack_at_ = now | 1;  // never 0
          ESP_LOGI(TAG, "%s took %u records%s", this->transport_->server(), (unsigned) n, this->transport_->ack_note());
        }
        break;
      }
      case SendResult::RETRY:
        this->flush_requested_ = true;
        break;
      case SendResult::FAILED:
        break;  // again at the next send interval (or sooner, on a stop)
    }
    this->in_flight_ = 0;
  }

  if (!network::is_connected())
    return;
  this->transport_->receive();
  if (this->backlog_.memory().empty())
    return;
  bool due = this->flush_requested_ || this->backlog_.memory().size() >= this->batch_max_ ||
             now - this->last_send_ >= this->send_interval_;
  if (due && this->transport_->ready())
    this->send_();
}

}  // namespace esphome::tracker_uplink
