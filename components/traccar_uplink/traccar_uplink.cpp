#include "traccar_uplink.h"

#include <cmath>

#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "esphome/components/network/util.h"

namespace esphome::traccar_uplink {

static const char *const TAG = "traccar_uplink";

// Timing uses the loop's start time, as ESPHome asks of components.
static uint32_t now_ms() { return App.get_loop_component_start_time(); }

void TraccarUplink::setup() {
  // Everything that needs memory is allocated here, once.
  this->queue_.init(this->max_queue_);
  this->batch_ = std::make_unique<Record[]>(this->batch_max_);  // NOLINT(cppcoreguidelines-avoid-c-arrays)
  this->socket_ = socket::socket_udp(AF_INET, IPPROTO_IP);
  if (!this->socket_) {
    ESP_LOGE(TAG, "Could not create a UDP socket");
    this->mark_failed();
    return;
  }
  this->socket_->setblocking(false);
  this->last_tick_ = now_ms();
}

void TraccarUplink::dump_config() {
  const TrackerConfig &c = this->tracker_.config();
  ESP_LOGCONFIG(TAG,
                "Traccar uplink (Teltonika GH3000 over UDP):\n"
                "  Server: %s:%u\n"
                "  Device ID: %s\n"
                "  Send interval: %" PRIu32 " s, ack timeout %" PRIu32 " s\n"
                "  Queue: %u records, up to %u per packet\n"
                "  Moving at %.0f km/h, a record every %" PRIu32 " s or on a %.0f deg turn\n"
                "  Stopped after %" PRIu32 " s below that; heartbeat every %" PRIu32 " s",
                this->host_.c_str(), this->port_, this->device_id_.c_str(), this->send_interval_ / 1000,
                this->ack_timeout_ / 1000, this->max_queue_, this->batch_max_, c.moving_speed_kmh, c.moving_interval_s,
                c.turn_angle_deg, c.stop_delay_s, c.heartbeat_s);
  // Shown whenever a log viewer connects, so it says how the uplink is doing now.
  ESP_LOGCONFIG(TAG, "  Waiting: %u records; acknowledged since boot: %" PRIu32 " (%" PRIu32 " bytes sent)",
                (unsigned) this->queue_size(), this->records_sent_, this->bytes_sent_);
  if (this->last_ack_at_ != 0) {
    ESP_LOGCONFIG(TAG, "  Last answer from Traccar: %" PRIu32 " s ago", (now_ms() - this->last_ack_at_) / 1000);
  } else {
    ESP_LOGCONFIG(TAG, "  Last answer from Traccar: none since boot");
  }
}

bool TraccarUplink::now_(uint32_t *unix) {
  if (this->last_unix_ != 0) {
    *unix = this->last_unix_ + (now_ms() - this->last_unix_millis_) / 1000;
    return true;
  }
#ifdef USE_TIME
  if (this->time_ != nullptr) {
    auto t = this->time_->now();
    if (t.is_valid()) {
      *unix = t.timestamp;
      return true;
    }
  }
#endif
  return false;
}

Record TraccarUplink::base_record_(uint32_t time, Decision why) {
  Record r{};
  r.time = time;
  r.motion = -1;
  // Battery voltage and motion ride along on trip starts, stops and heartbeats only.
  if (why == Decision::START || why == Decision::STOP || why == Decision::HEARTBEAT) {
    r.motion = this->tracker_.moving() ? 1 : 0;
#ifdef USE_SENSOR
    if (this->battery_ != nullptr && this->battery_->has_state() && !std::isnan(this->battery_->state))
      r.battery_mv = static_cast<uint16_t>(this->battery_->state * 1000.0f);
#endif
  }
  return r;
}

void TraccarUplink::add_fix(uint32_t ddmmyy, uint32_t hhmmsscc, float latitude, float longitude, float speed_kmh,
                            float course_deg, uint8_t satellites) {
  uint32_t unix = ::trackcodec::gnss_to_unix(ddmmyy, hhmmsscc);
  if (unix == 0 && !this->now_(&unix))
    return;  // no time yet: nothing useful to record
  this->last_unix_ = unix;
  this->last_unix_millis_ = now_ms();

  Decision why = this->tracker_.on_fix(unix, speed_kmh, course_deg);
  if (why == Decision::NONE)
    return;
  Record r = this->base_record_(unix, why);
  r.has_location = true;
  r.latitude = latitude;
  r.longitude = longitude;
  r.speed_kmh = speed_kmh;
  r.course_deg = course_deg;
  r.satellites = satellites;
  this->enqueue_(r);

  if (why == Decision::START) {
    ESP_LOGI(TAG, "Trip started");
    this->motion_start_trigger_.trigger();
  } else if (why == Decision::STOP) {
    ESP_LOGI(TAG, "Trip stopped");
    this->flush_requested_ = true;  // the parking spot goes out now
    this->motion_stop_trigger_.trigger();
  } else if (why == Decision::HEARTBEAT) {
    this->flush_requested_ = true;
  }
}

void TraccarUplink::enqueue_(const Record &record) {
  // Never drop a record that's in the packet waiting for its answer.
  if (this->queue_.full() && this->queue_.size() > this->in_flight_count_) {
    ESP_LOGW(TAG, "Queue full: dropping the oldest unsent record");
    this->queue_.erase(this->in_flight_count_);
  }
  if (!this->queue_.push_back(record))
    ESP_LOGW(TAG, "Queue full: record dropped");
  ESP_LOGD(TAG, "Queued a record (%u waiting)", (unsigned) this->queue_.size());
}

void TraccarUplink::receive_() {
  uint8_t buf[16];
  for (int i = 0; i < 4; i++) {
    ssize_t len = this->socket_->recvfrom(buf, sizeof(buf), nullptr, nullptr);
    if (len <= 0)
      return;
    if (!this->in_flight_)
      continue;
    int accepted = ::trackcodec::parse_ack(buf, len, this->location_packet_id_);
    if (accepted < 0)
      continue;
    size_t n = std::min<size_t>(this->in_flight_count_, this->queue_.size());
    this->queue_.pop_front(n);
    this->records_sent_ += n;
    this->last_ack_at_ = now_ms() | 1;  // never 0
    if ((size_t) accepted != n)
      ESP_LOGW(TAG, "Traccar accepted %d of %u records", accepted, (unsigned) n);
    else
      ESP_LOGD(TAG, "Traccar acknowledged %u records", (unsigned) n);
    this->in_flight_ = false;
    this->in_flight_count_ = 0;
    this->failures_ = 0;
    this->location_packet_id_++;
  }
}

void TraccarUplink::send_() {
  if (this->queue_.empty())
    return;
  if (!this->resolver_.ready()) {
    if (this->resolver_.consume_failure())
      this->resolve_retry_at_ = now_ms() + 30000;
    if ((int32_t) (now_ms() - this->resolve_retry_at_) >= 0)
      this->resolver_.start(this->host_.c_str(), this->port_, TAG);
    return;
  }
  size_t count = std::min<size_t>(this->queue_.size(), this->batch_max_);
  this->queue_.copy(0, count, this->batch_.get());
  size_t len = 0;
  while (count > 0 &&
         (len = ::trackcodec::encode_packet(this->buf_, sizeof(this->buf_), this->packet_id_, this->location_packet_id_,
                                            this->device_id_.c_str(), this->batch_.get(), count)) == 0)
    count--;
  if (len == 0) {
    ESP_LOGE(TAG, "Could not encode a packet (device ID too long?)");
    return;
  }

  struct sockaddr_storage addr;
  socklen_t addr_len = this->resolver_.to_sockaddr((struct sockaddr *) &addr, sizeof(addr), this->port_);
  ssize_t sent = this->socket_->sendto(this->buf_, len, 0, (struct sockaddr *) &addr, addr_len);
  if (sent != (ssize_t) len) {
    ESP_LOGW(TAG, "Send failed (errno %d); trying again at the next send interval", errno);
    this->last_send_ = now_ms();
    this->flush_requested_ = false;
    return;
  }
  this->packet_id_++;
  this->bytes_sent_ += len;
  this->in_flight_ = true;
  this->in_flight_count_ = count;
  this->sent_at_ = now_ms();
  this->last_send_ = now_ms();
  this->flush_requested_ = false;
  ESP_LOGD(TAG, "Sent %u records in %u bytes", (unsigned) count, (unsigned) len);
}

void TraccarUplink::loop() {
  uint32_t now = now_ms();

  // Heartbeats while parked without a fix (the tracker ignores this while fixes flow).
  if (now - this->last_tick_ >= 1000) {
    this->last_tick_ = now;
    uint32_t unix;
    if (this->now_(&unix) && this->tracker_.on_tick(unix) == Decision::HEARTBEAT) {
      this->enqueue_(this->base_record_(unix, Decision::HEARTBEAT));
      this->flush_requested_ = true;
    }
  }

  if (!network::is_connected())
    return;

  this->receive_();

  if (this->in_flight_) {
    if (now - this->sent_at_ < this->ack_timeout_)
      return;
    // No answer: send the same records again in a new packet.
    this->in_flight_ = false;
    this->in_flight_count_ = 0;
    this->location_packet_id_++;
    if (++this->failures_ >= 3) {
      ESP_LOGW(TAG, "No answer from %s after %u tries; resolving it again", this->host_.c_str(), this->failures_);
      this->resolver_.forget();
      this->failures_ = 0;
      this->last_send_ = now;  // back off until the next send interval
      return;
    }
    this->flush_requested_ = true;
  }

  bool due = this->flush_requested_ || this->queue_.size() >= this->batch_max_ ||
             now - this->last_send_ >= this->send_interval_;
  if (due)
    this->send_();
}

}  // namespace esphome::traccar_uplink
