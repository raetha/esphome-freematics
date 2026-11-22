#include "webhook.h"

#ifdef USE_UPLINK_WEBHOOK

#include <cstring>

#include <esp_crt_bundle.h>

#include "esphome/core/log.h"

namespace esphome::tracker_uplink {

static const char *const TAG = "tracker_uplink.webhook";

bool WebhookPoster::start(const std::string &url, size_t max_body) {
  this->body_ = std::make_unique<uint8_t[]>(max_body);  // NOLINT(cppcoreguidelines-avoid-c-arrays)
  this->max_body_ = max_body;
  esp_http_client_config_t config{};
  config.url = url.c_str();  // copied by esp_http_client_init
  config.method = HTTP_METHOD_POST;
  config.timeout_ms = 20000;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.user_agent = "freematics";  // short: it's sent with every request
  config.buffer_size = 512;
  config.buffer_size_tx = 512;
  config.disable_auto_redirect = true;
  // No TCP keep-alive probes: they would cost data between batches. HTTP/1.1
  // keeps the connection itself open while the server does.
#if CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS
  config.save_client_session = true;
#endif
  this->client_ = esp_http_client_init(&config);
  if (this->client_ == nullptr)
    return false;
  esp_http_client_set_header(this->client_, "Content-Type", "application/octet-stream");
  return xTaskCreate(WebhookPoster::task_, "webhook", 8192, this, 1, &this->task_handle_) == pdPASS;
}

bool WebhookPoster::post(const uint8_t *body, size_t len) {
  if (this->state_.load() != IDLE || len > this->max_body_)
    return false;
  std::memcpy(this->body_.get(), body, len);
  this->len_ = len;
  this->status_.store(0);
  this->state_.store(QUEUED);
  xTaskNotifyGive(this->task_handle_);
  return true;
}

int WebhookPoster::take_result() {
  if (this->state_.load() != DONE)
    return 0;
  int status = this->status_.load();
  this->state_.store(IDLE);
  return status;
}

int WebhookPoster::perform_() {
  esp_http_client_set_post_field(this->client_, reinterpret_cast<const char *>(this->body_.get()),
                                 static_cast<int>(this->len_));
  esp_err_t err = esp_http_client_perform(this->client_);
  if (err != ESP_OK) {
    // A kept connection the server has since closed fails here: once more on a new one.
    esp_http_client_close(this->client_);
    err = esp_http_client_perform(this->client_);
  }
  if (err != ESP_OK) {
    this->error_.store(err);
    esp_http_client_close(this->client_);
    return -1;
  }
  return esp_http_client_get_status_code(this->client_);
}

void WebhookPoster::task_(void *arg) {
  auto *self = static_cast<WebhookPoster *>(arg);
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (self->state_.load() != QUEUED)
      continue;
    self->status_.store(self->perform_());
    self->state_.store(DONE);
  }
}

}  // namespace esphome::tracker_uplink

#endif  // USE_UPLINK_WEBHOOK
