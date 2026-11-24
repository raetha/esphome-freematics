#pragma once

#include "esphome/core/defines.h"

#ifdef USE_UPLINK_WEBHOOK

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <esp_http_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "transport.h"

namespace esphome::tracker_uplink {

/// Posts a body to an HTTPS URL (a Home Assistant webhook) from a task of its
/// own, so the main loop never waits for DNS, TCP, TLS or the server. One
/// request at a time; the connection is kept open between requests when the
/// server allows, and a TLS session ticket shortens the next handshake when it
/// doesn't.
class WebhookPoster {
 public:
  /// Allocate the body buffer and start the task. Call once, from setup().
  bool start(const std::string &url, size_t max_body);
  /// Whether a post can be made now.
  bool idle() const { return this->state_.load() == IDLE; }
  /// Post the body as base64 text (see compact.h). False when a post is under
  /// way or it's too big.
  bool post(const uint8_t *body, size_t len);
  /// While a post runs, 0; once it's done, its HTTP status, or -1 when there
  /// was no answer; reading a finished result makes the poster idle again.
  int take_result();
  /// The ESP-IDF error of the last post that got no answer.
  esp_err_t last_error() const { return this->error_.load(); }
  /// Connections the last post opened: 0 it went over the kept one, 1 a new
  /// one, 2 the kept one had closed and the request went again over a new one.
  uint8_t last_connects() const { return this->last_connects_.load(); }

 protected:
  enum State : uint8_t { IDLE, QUEUED, DONE };
  static void task_(void *arg);
  static esp_err_t on_event_(esp_http_client_event_t *event);
  int perform_();
  int perform_once_();

  std::atomic<uint8_t> state_{IDLE};
  std::atomic<int> status_{0};
  std::atomic<esp_err_t> error_{ESP_OK};
  uint8_t connects_{0};  // counted by on_event_, in the poster's task
  std::atomic<uint8_t> last_connects_{0};
  std::unique_ptr<uint8_t[]> body_;  // NOLINT(cppcoreguidelines-avoid-c-arrays)
  size_t max_body_{0};
  size_t len_{0};
  esp_http_client_handle_t client_{nullptr};
  TaskHandle_t task_handle_{nullptr};
};

/// Compact batches (compact.h) posted to a Home Assistant webhook over HTTPS,
/// for the Freematics integration (raetha/ha-freematics); a 2xx answer
/// acknowledges a batch.
class WebhookTransport final : public Transport {
 public:
  void set_url(const std::string &url) { this->url_ = url; }
  const char *server() const override { return "Home Assistant"; }
  bool setup(size_t batch_max) override;
  void dump_config() override;
  bool ready() override { return this->poster_.idle(); }
  size_t send(const RecordQueue &records, size_t max) override;
  SendResult poll(uint32_t now) override;
  const char *ack_note() const override { return this->ack_note_; }
  void set_identity(const char *model, const char *firmware) override {
    this->model_ = model;
    this->firmware_ = firmware;
  }

 protected:
  std::string url_;
  // Sent on batches until one carrying them is acknowledged: once per boot.
  const char *model_{nullptr};
  const char *firmware_{nullptr};
  bool identity_sent_{false};
  bool identity_in_flight_{false};
  void note_connection_(uint32_t now);
  WebhookPoster poster_;
  uint16_t sequence_{0};
  size_t in_flight_{0};
  // How batches reached Home Assistant since boot: over the kept connection, a
  // new one, or a new one after the kept one had closed (the request sent twice).
  uint32_t batches_kept_{0};
  uint32_t batches_new_{0};
  uint32_t batches_resent_{0};
  uint32_t last_answer_ms_{0};  // 0: none yet
  char ack_note_[96]{};
  uint8_t buf_[1024];
};

}  // namespace esphome::tracker_uplink

#endif  // USE_UPLINK_WEBHOOK
