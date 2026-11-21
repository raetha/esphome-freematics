#pragma once

#include <cmath>

#include <memory>
#include <string>

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/components/socket/ipv4_resolve.h"
#include "esphome/components/socket/socket.h"
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_TIME
#include "esphome/components/time/real_time_clock.h"
#endif
#ifdef USE_FILE_QUEUE
#include "esphome/components/file_queue/file_queue.h"
#endif

#include "backlog.h"
#include "compact.h"
#include "freematics_text.h"
#include "teltonika.h"
#include "tracker.h"
#include "webhook.h"

namespace esphome::traccar_uplink {

using ::trackcodec::Decision;
using ::trackcodec::Record;
#ifdef USE_FILE_QUEUE
using Store = file_queue::FileQueue;
#else
using Store = ::trackcodec::NoStore;
#endif
using ::trackcodec::Tracker;
using ::trackcodec::TrackerConfig;

class TraccarUplink final : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

  void set_host(const std::string &host) { this->host_ = host; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_device_id(const std::string &id) { this->device_id_ = id; }
  /// Traccar's Freematics text protocol instead of GH3000: unacknowledged, so
  /// records count as delivered once sent; a login event checks the server answers.
  void set_freematics_protocol(bool on) { this->freematics_ = on; }
  /// Post compact batches to this Home Assistant webhook URL (HTTPS) instead
  /// of sending to Traccar; a 2xx answer acknowledges a batch.
  void set_webhook_url(const std::string &url) { this->webhook_url_ = url; }
  void set_altitude(bool on) { this->altitude_ = on; }
  void set_send_interval(uint32_t ms) { this->send_interval_ = ms; }
  void set_ack_timeout(uint32_t ms) { this->ack_timeout_ = ms; }
  void set_max_queue(uint16_t n) { this->max_queue_ = n; }
  void set_batch_max(uint8_t n) { this->batch_max_ = n; }
  void set_tracker_config(const TrackerConfig &config) { this->tracker_.set_config(config); }
#ifdef USE_SENSOR
  void set_battery_sensor(sensor::Sensor *s) { this->battery_ = s; }
#endif
#ifdef USE_TIME
  void set_time(time::RealTimeClock *t) { this->time_ = t; }
#endif
#ifdef USE_FILE_QUEUE
  /// Keep every record in this queue (an SD card file) until Traccar
  /// acknowledges it; memory then holds only the oldest max_queue of them.
  void set_file_queue(file_queue::FileQueue *q) { this->store_ = q; }
#endif

  /// A valid GNSS fix: date ddmmyy and time hhmmsscc (UTC) as the co-processor reports them.
  /// altitude_m: NAN when unknown; sent only with set_altitude(true).
  void add_fix(uint32_t ddmmyy, uint32_t hhmmsscc, float latitude, float longitude, float speed_kmh, float course_deg,
               uint8_t satellites, float altitude_m = NAN, uint8_t hdop = 0);
  /// Send what's queued at the next chance instead of waiting for send_interval.
  void flush() { this->flush_requested_ = true; }

  bool is_moving() const { return this->tracker_.moving(); }
  /// Whether a trip may start now (the car was moved lately); see Tracker::set_can_start.
  void set_can_start(bool can) { this->tracker_.set_can_start(can); }
  size_t queue_size() const { return this->backlog_.size(); }
  uint32_t records_sent() const { return this->records_sent_; }
  uint32_t bytes_sent() const { return this->bytes_sent_; }

  Trigger<> *get_motion_start_trigger() { return &this->motion_start_trigger_; }
  Trigger<> *get_motion_stop_trigger() { return &this->motion_stop_trigger_; }

 protected:
  void enqueue_(const Record &record);
  void send_freematics_(const struct sockaddr *addr, socklen_t addr_len);
  void send_webhook_();
  void check_webhook_();
  bool webhook_() const { return !this->webhook_url_.empty(); }
  bool now_(uint32_t *unix);
  void receive_();
  void send_();
  Record base_record_(uint32_t time, Decision why);

  std::string host_;
  uint16_t port_{5027};
  std::string device_id_;
  std::string webhook_url_;
#ifdef USE_UPLINK_WEBHOOK
  WebhookPoster poster_;
#endif
  uint16_t sequence_{0};
  uint32_t send_interval_{60000};
  uint32_t ack_timeout_{15000};
  uint16_t max_queue_{300};
  uint8_t batch_max_{40};
#ifdef USE_SENSOR
  sensor::Sensor *battery_{nullptr};
#endif
#ifdef USE_TIME
  time::RealTimeClock *time_{nullptr};
#endif
  Store *store_{nullptr};

  Tracker tracker_;
  ::trackcodec::Backlog<Store> backlog_;
  std::unique_ptr<Record[]> batch_;  // NOLINT(cppcoreguidelines-avoid-c-arrays)
  uint8_t buf_[1024];
  socket::Ipv4Resolve resolver_;
  std::unique_ptr<socket::UDPSocket> socket_;

  bool flush_requested_{false};
  bool in_flight_{false};
  uint8_t in_flight_count_{0};
  uint8_t location_packet_id_{0};
  bool freematics_{false};
  bool altitude_{true};
  bool login_answered_{false};
  uint32_t last_login_{0};
  uint16_t packet_id_{0};
  uint8_t failures_{0};
  uint32_t sent_at_{0};
  uint32_t last_send_{0};
  uint32_t last_tick_{0};
  uint32_t resolve_retry_at_{0};

  // Unix time of the last fix, and the loop time then, so time keeps counting between fixes.
  uint32_t last_unix_{0};
  uint32_t last_unix_millis_{0};

  uint32_t records_sent_{0};
  uint32_t last_ack_at_{0};  // loop time of Traccar's last answer; 0: none yet
  uint32_t bytes_sent_{0};

  Trigger<> motion_start_trigger_;
  Trigger<> motion_stop_trigger_;
};

template<typename... Ts> class FlushAction : public Action<Ts...>, public Parented<TraccarUplink> {
 public:
  void play(const Ts &...x) override { this->parent_->flush(); }
};

}  // namespace esphome::traccar_uplink
