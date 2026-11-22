#include "tracker_uplink.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/components/network/util.h"

namespace esphome::tracker_uplink {

static const char *const TAG = "tracker_uplink";

// Timing uses the loop's start time, as ESPHome asks of components.
static uint32_t now_ms() { return App.get_loop_component_start_time(); }

void TrackerUplink::setup() {
  // Everything that needs memory is allocated here, once.
  this->batch_ = std::make_unique<Record[]>(this->batch_max_);  // NOLINT(cppcoreguidelines-avoid-c-arrays)
  this->backlog_.init(this->max_queue_, this->store_);
  if (!this->backlog_.empty())
    ESP_LOGI(TAG, "%u records from before the restart to send", (unsigned) this->backlog_.size());
  this->last_tick_ = now_ms();
  if (this->webhook_()) {
#ifdef USE_UPLINK_WEBHOOK
    // A new start begins at a random batch number, so Home Assistant never
    // mistakes the first batches after a restart for ones it has seen.
    this->sequence_ = static_cast<uint16_t>(random_uint32());
    if (!this->poster_.start(this->webhook_url_, sizeof(this->buf_))) {
      ESP_LOGE(TAG, "Could not start the webhook client");
      this->mark_failed();
    }
#else
    this->mark_failed();
#endif
    return;
  }
  this->socket_ = socket::socket_udp(AF_INET, IPPROTO_IP);
  if (!this->socket_) {
    ESP_LOGE(TAG, "Could not create a UDP socket");
    this->mark_failed();
    return;
  }
  this->socket_->setblocking(false);
}

void TrackerUplink::dump_config() {
  const TrackerConfig &c = this->tracker_.config();
  if (this->webhook_()) {
    // The URL itself stays out of the log: the webhook ID in it is the only key.
    ESP_LOGCONFIG(TAG,
                  "Home Assistant uplink (compact batches to a webhook over HTTPS):\n"
                  "  Send interval: %" PRIu32 " s\n"
                  "  Queue: %u records, up to %u per batch\n"
                  "  Moving at %.0f km/h, a record every %" PRIu32 " s or on a %.0f deg turn\n"
                  "  Stopped after %" PRIu32 " s below that; heartbeat every %" PRIu32 " s\n"
                  "  Waiting: %u records; acknowledged since boot: %" PRIu32 " (%" PRIu32 " bytes sent)",
                  this->send_interval_ / 1000, this->max_queue_, this->batch_max_, c.moving_speed_kmh,
                  c.moving_interval_s, c.turn_angle_deg, c.stop_delay_s, c.heartbeat_s, (unsigned) this->queue_size(),
                  this->records_sent_, this->bytes_sent_);
    if (this->last_ack_at_ != 0) {
      ESP_LOGCONFIG(TAG, "  Last answer from Home Assistant: %" PRIu32 " s ago",
                    (now_ms() - this->last_ack_at_) / 1000);
    } else {
      ESP_LOGCONFIG(TAG, "  Last answer from Home Assistant: none since boot");
    }
    return;
  }
  ESP_LOGCONFIG(TAG,
                "Traccar uplink (%s over UDP):\n"
                "  Server: %s:%u\n"
                "  Device ID: %s\n"
                "  Send interval: %" PRIu32 " s, ack timeout %" PRIu32 " s\n"
                "  Queue: %u records, up to %u per packet\n"
                "  Moving at %.0f km/h, a record every %" PRIu32 " s or on a %.0f deg turn\n"
                "  Stopped after %" PRIu32 " s below that; heartbeat every %" PRIu32 " s",
                this->freematics_ ? "Freematics text, unacknowledged" : "Teltonika GH3000", this->host_.c_str(),
                this->port_, this->device_id_.c_str(), this->send_interval_ / 1000, this->ack_timeout_ / 1000,
                this->max_queue_, this->batch_max_, c.moving_speed_kmh, c.moving_interval_s, c.turn_angle_deg,
                c.stop_delay_s, c.heartbeat_s);
#ifdef USE_FILE_QUEUE
  if (this->store_ != nullptr)
    ESP_LOGCONFIG(TAG, "  Records kept on file until acknowledged (%s)",
                  this->backlog_.stored() ? "ready" : "unavailable");
#endif
  // Shown whenever a log viewer connects, so it says how the uplink is doing now.
  ESP_LOGCONFIG(TAG, "  Waiting: %u records; acknowledged since boot: %" PRIu32 " (%" PRIu32 " bytes sent)",
                (unsigned) this->queue_size(), this->records_sent_, this->bytes_sent_);
  if (this->last_ack_at_ != 0) {
    ESP_LOGCONFIG(TAG, "  Last answer from Traccar: %" PRIu32 " s ago", (now_ms() - this->last_ack_at_) / 1000);
  } else {
    ESP_LOGCONFIG(TAG, "  Last answer from Traccar: none since boot");
  }
}

bool TrackerUplink::now_(uint32_t *unix) {
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

Record TrackerUplink::base_record_(uint32_t time, Decision why) {
  Record r{};
  r.time = time;
  r.kind = static_cast<uint8_t>(why);
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

void TrackerUplink::add_fix(uint32_t ddmmyy, uint32_t hhmmsscc, float latitude, float longitude, float speed_kmh,
                            float course_deg, uint8_t satellites, float altitude_m, uint8_t hdop) {
  uint32_t unix = ::trackcodec::gnss_to_unix(ddmmyy, hhmmsscc);
  if (unix == 0 && !this->now_(&unix))
    return;  // no time yet: nothing useful to record
  this->last_unix_ = unix;
  this->last_unix_millis_ = now_ms();

  Decision why = this->tracker_.on_fix(unix, speed_kmh, course_deg, latitude, longitude);
  if (why == Decision::NONE)
    return;
  Record r = this->base_record_(unix, why);
  r.has_location = true;
  r.latitude = latitude;
  r.longitude = longitude;
  r.speed_kmh = speed_kmh;
  r.course_deg = course_deg;
  r.satellites = satellites;
  r.hdop = hdop;
  if (this->altitude_ && !std::isnan(altitude_m)) {
    r.has_altitude = true;
    r.altitude_m = static_cast<int16_t>(std::lround(std::clamp(altitude_m, -32768.0f, 32767.0f)));
  }
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

void TrackerUplink::enqueue_(const Record &record) {
  switch (this->backlog_.add(record, this->in_flight_count_)) {
    case ::trackcodec::Added::OK:
      break;
    case ::trackcodec::Added::DROPPED_OLDEST:
      ESP_LOGW(TAG, "Queue full: dropped the oldest unsent record");
      break;
    case ::trackcodec::Added::DROPPED:
      ESP_LOGW(TAG, "Queue full: record dropped");
      break;
  }
  ESP_LOGD(TAG, "Queued a record (%u waiting%s)", (unsigned) this->backlog_.size(),
           this->backlog_.stored() ? ", on file" : "");
}

void TrackerUplink::receive_() {
  uint8_t buf[32];
  for (int i = 0; i < 4; i++) {
    ssize_t len = this->socket_->recvfrom(buf, sizeof(buf), nullptr, nullptr);
    if (len <= 0)
      return;
    char hex[3 * sizeof(buf) + 1] = {};
    for (ssize_t j = 0; j < len && j < (ssize_t) sizeof(buf); j++)
      snprintf(hex + 3 * j, 4, "%02X ", buf[j]);
    if (this->freematics_) {
      if (::trackcodec::is_freematics_login_reply(buf, len)) {
        if (!this->login_answered_)
          ESP_LOGI(TAG, "Traccar answered the login: the server is reachable");
        this->login_answered_ = true;
        this->last_ack_at_ = now_ms() | 1;
      } else {
        ESP_LOGD(TAG, "Reply: %s", hex);
      }
      continue;
    }
    if (!this->in_flight_) {
      ESP_LOGD(TAG, "Reply with nothing waiting for one: %s", hex);
      continue;
    }
    int accepted = ::trackcodec::parse_ack(buf, len, this->location_packet_id_);
    if (accepted < 0) {
      ESP_LOGW(TAG, "Reply that isn't the acknowledgement expected (location packet %u): %s", this->location_packet_id_,
               hex);
      continue;
    }
    size_t n = std::min<size_t>(this->in_flight_count_, this->backlog_.memory().size());
    this->backlog_.acknowledged(n);
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

void TrackerUplink::send_() {
  if (this->backlog_.memory().empty())
    return;
  if (this->webhook_()) {
    this->send_webhook_();
    return;
  }
  if (!this->resolver_.ready()) {
    if (this->resolver_.consume_failure())
      this->resolve_retry_at_ = now_ms() + 30000;
    if ((int32_t) (now_ms() - this->resolve_retry_at_) >= 0)
      this->resolver_.start(this->host_.c_str(), this->port_, TAG);
    return;
  }
  if (this->freematics_) {
    struct sockaddr_storage addr;
    socklen_t addr_len = this->resolver_.to_sockaddr((struct sockaddr *) &addr, sizeof(addr), this->port_);
    this->send_freematics_((struct sockaddr *) &addr, addr_len);
    return;
  }
  size_t count = std::min<size_t>(this->backlog_.memory().size(), this->batch_max_);
  this->backlog_.memory().copy(0, count, this->batch_.get());
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

void TrackerUplink::send_freematics_(const struct sockaddr *addr, socklen_t addr_len) {
  uint32_t now = now_ms();
  char *out = reinterpret_cast<char *>(this->buf_);
  // A login until Traccar answers one (then every hour), to show it's listening.
  if (!this->login_answered_ ? now - this->last_login_ >= 60000 || this->last_login_ == 0
                             : now - this->last_login_ >= 3600000) {
    size_t len = ::trackcodec::encode_freematics_login(out, sizeof(this->buf_), this->device_id_.c_str(), now);
    if (len != 0 && this->socket_->sendto(out, len, 0, addr, addr_len) == (ssize_t) len) {
      this->bytes_sent_ += len;
      ESP_LOGD(TAG, "Sent a login (%u bytes)", (unsigned) len);
    }
    this->last_login_ = now | 1;
  }
  size_t count = std::min<size_t>(this->backlog_.memory().size(), this->batch_max_);
  this->backlog_.memory().copy(0, count, this->batch_.get());
  size_t len = 0;
  while (count > 0 && (len = ::trackcodec::encode_freematics(out, sizeof(this->buf_), this->device_id_.c_str(),
                                                             this->batch_.get(), count)) == 0)
    count--;
  this->last_send_ = now;
  this->flush_requested_ = false;
  if (len == 0) {
    ESP_LOGE(TAG, "Could not encode a packet (device ID too long?)");
    return;
  }
  if (this->socket_->sendto(out, len, 0, addr, addr_len) != (ssize_t) len) {
    ESP_LOGW(TAG, "Send failed (errno %d); trying again at the next send interval", errno);
    return;
  }
  // Nothing comes back for positions: once sent, they count as delivered.
  this->backlog_.acknowledged(count);
  this->records_sent_ += count;
  this->bytes_sent_ += len;
  ESP_LOGD(TAG, "Sent %u records in %u bytes (Freematics, unacknowledged)", (unsigned) count, (unsigned) len);
}

static ::trackcodec::CompactKind compact_kind(uint8_t decision) {
  switch (static_cast<Decision>(decision)) {
    case Decision::START:
      return ::trackcodec::COMPACT_START;
    case Decision::STOP:
      return ::trackcodec::COMPACT_STOP;
    case Decision::HEARTBEAT:
      return ::trackcodec::COMPACT_HEARTBEAT;
    default:
      return ::trackcodec::COMPACT_FIX;
  }
}

void TrackerUplink::send_webhook_() {
#ifdef USE_UPLINK_WEBHOOK
  if (!this->poster_.idle())
    return;
  const auto &memory = this->backlog_.memory();
  ::trackcodec::CompactEncoder enc;
  enc.begin(this->buf_, sizeof(this->buf_), this->sequence_, memory.at(0).time);
  size_t count = 0;
  while (count < memory.size() && count < this->batch_max_ &&
         enc.add(memory.at(count), compact_kind(memory.at(count).kind)))
    count++;
  this->last_send_ = now_ms();
  this->flush_requested_ = false;
  if (count == 0 || !this->poster_.post(this->buf_, enc.size()))
    return;
  this->in_flight_ = true;
  this->in_flight_count_ = count;
  this->sent_at_ = now_ms();
  this->bytes_sent_ += enc.size();
  ESP_LOGD(TAG, "Posting %u records in %u bytes", (unsigned) count, (unsigned) enc.size());
#endif
}

void TrackerUplink::check_webhook_() {
#ifdef USE_UPLINK_WEBHOOK
  int status = this->poster_.take_result();
  if (status == 0)
    return;  // still under way
  size_t n = std::min<size_t>(this->in_flight_count_, this->backlog_.memory().size());
  this->in_flight_ = false;
  this->in_flight_count_ = 0;
  this->sequence_++;
  if (status >= 200 && status < 300) {
    this->backlog_.acknowledged(n);
    this->records_sent_ += n;
    this->last_ack_at_ = now_ms() | 1;
    this->failures_ = 0;
    ESP_LOGD(TAG, "Home Assistant took %u records", (unsigned) n);
    return;
  }
  // Tried again at the next send interval (or sooner, on a stop).
  this->failures_++;
  if (status < 0) {
    ESP_LOGW(TAG, "No answer from Home Assistant (%s); %u records kept", esp_err_to_name(this->poster_.last_error()),
             (unsigned) n);
  } else if (status == 404 || status == 405) {
    ESP_LOGW(TAG, "Home Assistant doesn't know this webhook (HTTP %d): check the URL and the integration", status);
  } else {
    ESP_LOGW(TAG, "Home Assistant answered HTTP %d; %u records kept", status, (unsigned) n);
  }
#endif
}

void TrackerUplink::loop() {
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

  if (this->webhook_()) {
    if (this->in_flight_) {
      this->check_webhook_();
      return;
    }
    if (!network::is_connected())
      return;
    if (this->flush_requested_ || this->backlog_.memory().size() >= this->batch_max_ ||
        now - this->last_send_ >= this->send_interval_)
      this->send_();
    return;
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

  bool due = this->flush_requested_ || this->backlog_.memory().size() >= this->batch_max_ ||
             now - this->last_send_ >= this->send_interval_;
  if (due)
    this->send_();
}

}  // namespace esphome::tracker_uplink
