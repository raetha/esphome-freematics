#pragma once

#include "esphome/core/defines.h"

#ifdef USE_UPLINK_UDP

#include <memory>
#include <string>

#include "esphome/components/socket/ipv4_resolve.h"
#include "esphome/components/socket/socket.h"

#include "teltonika.h"
#include "transport.h"

namespace esphome::tracker_uplink {

/// A UDP server found by name, looked up again when it stops answering.
class UdpTransport : public Transport {
 public:
  void set_host(const std::string &host) { this->host_ = host; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_device_id(const std::string &id) { this->device_id_ = id; }
  const char *server() const override { return "Traccar"; }
  bool setup(size_t batch_max) override;
  bool ready() override;

 protected:
  bool send_bytes_(const uint8_t *data, size_t len);
  /// Copy up to max records into batch_ (contiguous, for the encoders).
  size_t take_(const RecordQueue &records, size_t max);

  std::string host_;
  uint16_t port_{0};
  std::string device_id_;
  socket::Ipv4Resolve resolver_;
  std::unique_ptr<socket::UDPSocket> socket_;
  uint32_t resolve_retry_at_{0};
  std::unique_ptr<::trackcodec::Record[]> batch_;  // NOLINT(cppcoreguidelines-avoid-c-arrays)
  size_t batch_max_{0};
  uint8_t buf_[1024];
};

/// Teltonika GH3000 records (teltonika.h): the densest format Traccar reads,
/// and acknowledged packet by packet.
class TeltonikaTransport final : public UdpTransport {
 public:
  void set_ack_timeout(uint32_t ms) { this->ack_timeout_ = ms; }
  void dump_config() override;
  size_t send(const RecordQueue &records, size_t max) override;
  SendResult poll(uint32_t now) override;
  void receive() override;

 protected:
  /// Reads replies; returns the records the expected acknowledgement accepted, or -1.
  int read_ack_();

  uint32_t ack_timeout_{15000};
  uint16_t packet_id_{0};
  uint8_t location_packet_id_{0};
  size_t in_flight_{0};
  uint32_t sent_at_{0};
  uint8_t failures_{0};
};

/// Traccar's Freematics text protocol (freematics_text.h), what the stock
/// firmware sent: unacknowledged, so records count as delivered once sent; a
/// login event, which Traccar answers, shows the server is listening.
class FreematicsTransport final : public UdpTransport {
 public:
  bool acknowledged() const override { return false; }
  void dump_config() override;
  size_t send(const RecordQueue &records, size_t max) override;
  SendResult poll(uint32_t now) override { return SendResult::ACKED; }
  void receive() override;

 protected:
  bool login_answered_{false};
  uint32_t last_login_{0};
};

}  // namespace esphome::tracker_uplink

#endif  // USE_UPLINK_UDP
