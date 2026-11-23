#pragma once

#include <cstddef>
#include <cstdint>

#include "record_queue.h"

namespace esphome::tracker_uplink {

using ::trackcodec::RecordQueue;

/// How a send turned out.
enum class SendResult : uint8_t {
  PENDING,  // no answer yet
  ACKED,    // the server has the records
  RETRY,    // lost: send them again now
  FAILED,   // not delivered: try again at the next send interval
};

/// One way of getting records to a server: a protocol and its connection.
/// TrackerUplink decides what to send and when, keeps the records until they
/// are delivered, and drives a transport through send() and poll(). A new
/// protocol is a new Transport, its codec in a plain header, and an entry in
/// the component's schema (transports.py).
class Transport {
 public:
  virtual ~Transport() = default;
  /// Allocate what it needs, once; batch_max is the most records a send takes.
  virtual bool setup(size_t batch_max) = 0;
  virtual void dump_config() = 0;
  /// The server's name, for the log.
  virtual const char *server() const = 0;
  /// Whether the server says what arrived; otherwise records count as
  /// delivered once sent.
  virtual bool acknowledged() const { return true; }
  /// Whether the transport is ready to send now (an address resolved, the
  /// previous request finished); may start what that takes.
  virtual bool ready() = 0;
  /// Send up to max records from the front of records. Returns how many went;
  /// 0 when nothing could be sent now.
  virtual size_t send(const RecordQueue &records, size_t max) = 0;
  /// While a send awaits its answer: called each loop until it isn't PENDING.
  virtual SendResult poll(uint32_t now) = 0;
  /// Each loop while the network is up: read what the server sent unasked.
  virtual void receive() {}
  /// The device's model and firmware version, for protocols that carry them
  /// (strings that live as long as the program).
  virtual void set_identity(const char *model, const char *firmware) {}
  uint32_t bytes_sent() const { return this->bytes_sent_; }

 protected:
  uint32_t bytes_sent_{0};
};

}  // namespace esphome::tracker_uplink
