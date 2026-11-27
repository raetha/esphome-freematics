#pragma once

#include <cmath>

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/components/trip_tracker/trip_tracker.h"
#ifdef USE_FILE_QUEUE
#include "esphome/components/file_queue/file_queue.h"
#endif

#include "backlog.h"
#include "teltonika.h"
#include "tracker.h"
#include "transport.h"

namespace esphome::tracker_uplink {

using ::trackcodec::Decision;
using ::trackcodec::Record;
#ifdef USE_FILE_QUEUE
using Store = file_queue::FileQueue;
#else
using Store = ::trackcodec::NoStore;
#endif

/// Reports one car's trips (trip_tracker) to one server: decides which fixes
/// become records (tracker.h's Sampler: a record every few seconds while
/// moving, more on turns, heartbeats while parked), keeps them, in memory or
/// on an SD card file, until the server has them, and sends them in batches
/// through its transport (Teltonika or Freematics to Traccar over UDP, or
/// compact batches to a Home Assistant webhook). Several can report the same
/// trips, each to its own server.
class TrackerUplink final : public Component,
                            public Parented<trip_tracker::TripTracker>,
                            public trip_tracker::Listener {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

  void set_transport(Transport *t) { this->transport_ = t; }
  void set_sample_config(const ::trackcodec::SampleConfig &config) { this->sampler_.set_config(config); }
  void set_altitude(bool on) { this->altitude_ = on; }
  void set_send_interval(uint32_t ms) { this->send_interval_ = ms; }
  void set_max_queue(uint16_t n) { this->max_queue_ = n; }
  void set_batch_max(uint8_t n) { this->batch_max_ = n; }
#ifdef USE_FILE_QUEUE
  /// Keep every record in this queue (an SD card file) until the server
  /// acknowledges it; memory then holds only the oldest max_queue of them.
  void set_file_queue(file_queue::FileQueue *q) { this->store_ = q; }
#endif

  // trip_tracker::Listener
  void on_trip_fix(const trip_tracker::Fix &fix, trip_tracker::Trip trip, bool moving, bool fast,
                   const trip_tracker::TripStart *start) override;
  void on_trip_note(uint32_t time, uint8_t impact_dg, uint16_t next_report_min) override;
  size_t queued() const override { return this->backlog_.size(); }
  bool delivered_any() const override { return this->records_sent_ > 0; }
  /// Send what's queued at the next chance instead of waiting for send_interval.
  void flush() override { this->flush_requested_ = true; }

  uint32_t records_sent() const { return this->records_sent_; }
  uint32_t bytes_sent() const { return this->transport_->bytes_sent(); }

 protected:
  void enqueue_(const Record &record);
  Record base_record_(uint32_t time, Decision why) const;
  void set_location_(Record &r, const trip_tracker::Fix &fix) const;
  void set_extras_(Record &r) const;
  void send_();

  Transport *transport_{nullptr};
  ::trackcodec::Sampler sampler_;
  uint32_t send_interval_{60000};
  uint16_t max_queue_{300};
  uint8_t batch_max_{40};
  bool altitude_{true};
  Store *store_{nullptr};
  ::trackcodec::Backlog<Store> backlog_;

  bool flush_requested_{false};
  size_t in_flight_{0};  // records sent and awaiting the server's answer
  uint32_t last_send_{0};
  uint32_t last_tick_{0};
  uint32_t records_sent_{0};
  uint32_t last_ack_at_{0};  // loop time of the server's last answer; 0: none yet
};

template<typename... Ts> class FlushAction : public Action<Ts...>, public Parented<TrackerUplink> {
 public:
  void play(const Ts &...x) override { this->parent_->flush(); }
};

}  // namespace esphome::tracker_uplink
