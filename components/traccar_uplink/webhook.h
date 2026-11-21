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

namespace esphome::traccar_uplink {

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
  /// Copy the body and post it. False when a post is under way or it's too big.
  bool post(const uint8_t *body, size_t len);
  /// While a post runs, 0; once it's done, its HTTP status, or -1 when there
  /// was no answer; reading a finished result makes the poster idle again.
  int take_result();
  /// The ESP-IDF error of the last post that got no answer.
  esp_err_t last_error() const { return this->error_.load(); }

 protected:
  enum State : uint8_t { IDLE, QUEUED, DONE };
  static void task_(void *arg);
  int perform_();

  std::atomic<uint8_t> state_{IDLE};
  std::atomic<int> status_{0};
  std::atomic<esp_err_t> error_{ESP_OK};
  std::unique_ptr<uint8_t[]> body_;  // NOLINT(cppcoreguidelines-avoid-c-arrays)
  size_t max_body_{0};
  size_t len_{0};
  esp_http_client_handle_t client_{nullptr};
  TaskHandle_t task_handle_{nullptr};
};

}  // namespace esphome::traccar_uplink

#endif  // USE_UPLINK_WEBHOOK
