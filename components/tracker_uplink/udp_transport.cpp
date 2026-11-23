#include "udp_transport.h"

#ifdef USE_UPLINK_UDP

#include <algorithm>
#include <cstdio>

#include "esphome/core/application.h"
#include "esphome/core/log.h"

#include "freematics_text.h"

namespace esphome::tracker_uplink {

static const char *const TAG = "tracker_uplink.udp";

static uint32_t now_ms() { return App.get_loop_component_start_time(); }

bool UdpTransport::setup(size_t batch_max) {
  this->batch_ = std::make_unique<::trackcodec::Record[]>(batch_max);  // NOLINT(cppcoreguidelines-avoid-c-arrays)
  this->batch_max_ = batch_max;
  this->socket_ = socket::socket_udp(AF_INET, IPPROTO_IP);
  if (!this->socket_) {
    ESP_LOGE(TAG, "Could not create a UDP socket");
    return false;
  }
  this->socket_->setblocking(false);
  return true;
}

bool UdpTransport::ready() {
  if (this->resolver_.ready())
    return true;
  if (this->resolver_.consume_failure())
    this->resolve_retry_at_ = now_ms() + 30000;
  if ((int32_t) (now_ms() - this->resolve_retry_at_) >= 0)
    this->resolver_.start(this->host_.c_str(), this->port_, TAG);
  return false;
}

bool UdpTransport::send_bytes_(const uint8_t *data, size_t len) {
  struct sockaddr_storage addr;
  socklen_t addr_len = this->resolver_.to_sockaddr((struct sockaddr *) &addr, sizeof(addr), this->port_);
  if (this->socket_->sendto(data, len, 0, (struct sockaddr *) &addr, addr_len) != (ssize_t) len) {
    ESP_LOGW(TAG, "Send failed (errno %d); trying again at the next send interval", errno);
    return false;
  }
  this->bytes_sent_ += len;
  return true;
}

size_t UdpTransport::take_(const RecordQueue &records, size_t max) {
  size_t count = std::min({records.size(), max, this->batch_max_});
  records.copy(0, count, this->batch_.get());
  return count;
}

// ---- Teltonika GH3000 ----

void TeltonikaTransport::dump_config() {
  ESP_LOGCONFIG(TAG,
                "  Protocol: Teltonika GH3000 over UDP, acknowledged\n"
                "  Server: %s:%u\n"
                "  Device ID: %s\n"
                "  Acknowledgement timeout: %" PRIu32 " s",
                this->host_.c_str(), this->port_, this->device_id_.c_str(), this->ack_timeout_ / 1000);
}

size_t TeltonikaTransport::send(const RecordQueue &records, size_t max) {
  size_t count = this->take_(records, max);
  size_t len = 0;
  while (count > 0 &&
         (len = ::trackcodec::encode_packet(this->buf_, sizeof(this->buf_), this->packet_id_, this->location_packet_id_,
                                            this->device_id_.c_str(), this->batch_.get(), count)) == 0)
    count--;
  if (len == 0) {
    ESP_LOGE(TAG, "Could not encode a packet (device ID too long?)");
    return 0;
  }
  if (!this->send_bytes_(this->buf_, len))
    return 0;
  this->packet_id_++;
  this->in_flight_ = count;
  this->sent_at_ = now_ms();
  ESP_LOGD(TAG, "Sent %u records in %u bytes", (unsigned) count, (unsigned) len);
  return count;
}

int TeltonikaTransport::read_ack_() {
  uint8_t buf[32];
  for (int i = 0; i < 4; i++) {
    ssize_t len = this->socket_->recvfrom(buf, sizeof(buf), nullptr, nullptr);
    if (len <= 0)
      return -1;
    char hex[3 * sizeof(buf) + 1] = {};
    for (ssize_t j = 0; j < len && j < (ssize_t) sizeof(buf); j++)
      snprintf(hex + 3 * j, 4, "%02X ", buf[j]);
    if (this->in_flight_ == 0) {
      ESP_LOGD(TAG, "Reply with nothing waiting for one: %s", hex);
      continue;
    }
    int accepted = ::trackcodec::parse_ack(buf, len, this->location_packet_id_);
    if (accepted >= 0)
      return accepted;
    ESP_LOGW(TAG, "Reply that isn't the acknowledgement expected (location packet %u): %s", this->location_packet_id_,
             hex);
  }
  return -1;
}

void TeltonikaTransport::receive() {
  if (this->in_flight_ == 0)
    this->read_ack_();  // logs what arrives unasked
}

SendResult TeltonikaTransport::poll(uint32_t now) {
  int accepted = this->read_ack_();
  if (accepted >= 0) {
    if ((size_t) accepted != this->in_flight_)
      ESP_LOGW(TAG, "Traccar accepted %d of %u records", accepted, (unsigned) this->in_flight_);
    this->in_flight_ = 0;
    this->failures_ = 0;
    this->location_packet_id_++;
    return SendResult::ACKED;
  }
  if (now - this->sent_at_ < this->ack_timeout_)
    return SendResult::PENDING;
  // No answer: the same records go again in a new packet.
  this->in_flight_ = 0;
  this->location_packet_id_++;
  if (++this->failures_ >= 3) {
    ESP_LOGW(TAG, "No answer from %s after %u tries; resolving it again", this->host_.c_str(), this->failures_);
    this->resolver_.forget();
    this->failures_ = 0;
    return SendResult::FAILED;
  }
  return SendResult::RETRY;
}

// ---- Freematics text ----

void FreematicsTransport::dump_config() {
  ESP_LOGCONFIG(TAG,
                "  Protocol: Freematics text over UDP, unacknowledged\n"
                "  Server: %s:%u\n"
                "  Device ID: %s\n"
                "  Login answered: %s",
                this->host_.c_str(), this->port_, this->device_id_.c_str(), YESNO(this->login_answered_));
}

void FreematicsTransport::receive() {
  uint8_t buf[32];
  for (int i = 0; i < 4; i++) {
    ssize_t len = this->socket_->recvfrom(buf, sizeof(buf), nullptr, nullptr);
    if (len <= 0)
      return;
    if (::trackcodec::is_freematics_login_reply(buf, len)) {
      if (!this->login_answered_)
        ESP_LOGI(TAG, "Traccar answered the login: the server is reachable");
      this->login_answered_ = true;
    }
  }
}

size_t FreematicsTransport::send(const RecordQueue &records, size_t max) {
  uint32_t now = now_ms();
  char *out = reinterpret_cast<char *>(this->buf_);
  // A login until Traccar answers one (then every hour), to show it's listening.
  if (!this->login_answered_ ? now - this->last_login_ >= 60000 || this->last_login_ == 0
                             : now - this->last_login_ >= 3600000) {
    size_t len = ::trackcodec::encode_freematics_login(out, sizeof(this->buf_), this->device_id_.c_str(), now);
    if (len != 0 && this->send_bytes_(this->buf_, len)) {
      ESP_LOGD(TAG, "Sent a login (%u bytes)", (unsigned) len);
    }
    this->last_login_ = now | 1;
  }
  size_t count = this->take_(records, max);
  size_t len = 0;
  while (count > 0 && (len = ::trackcodec::encode_freematics(out, sizeof(this->buf_), this->device_id_.c_str(),
                                                             this->batch_.get(), count)) == 0)
    count--;
  if (len == 0) {
    ESP_LOGE(TAG, "Could not encode a packet (device ID too long?)");
    return 0;
  }
  if (!this->send_bytes_(this->buf_, len))
    return 0;
  ESP_LOGD(TAG, "Sent %u records in %u bytes (Freematics, unacknowledged)", (unsigned) count, (unsigned) len);
  return count;
}

}  // namespace esphome::tracker_uplink

#endif  // USE_UPLINK_UDP
