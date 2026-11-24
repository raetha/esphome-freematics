#include "webhook.h"

#ifdef USE_UPLINK_WEBHOOK

#include <cinttypes>
#include <cstdio>
#include <cstring>

#include <esp_crt_bundle.h>

#include "compact.h"
#include "tracker.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::tracker_uplink {

static const char *const TAG = "tracker_uplink.webhook";

bool WebhookPoster::start(const std::string &url, size_t max_body) {
  this->body_ =
      std::make_unique<uint8_t[]>(::trackcodec::base64_size(max_body));  // NOLINT(cppcoreguidelines-avoid-c-arrays)
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
  config.event_handler = WebhookPoster::on_event_;
  config.user_data = this;
  // No TCP keep-alive probes: they would cost data between batches. HTTP/1.1
  // keeps the connection itself open while the server does.
#if CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS
  config.save_client_session = true;
#endif
  this->client_ = esp_http_client_init(&config);
  if (this->client_ == nullptr)
    return false;
  esp_http_client_set_header(this->client_, "Content-Type", "text/plain");
  return xTaskCreate(WebhookPoster::task_, "webhook", 8192, this, 1, &this->task_handle_) == pdPASS;
}

bool WebhookPoster::post(const uint8_t *body, size_t len) {
  if (this->state_.load() != IDLE || len > this->max_body_)
    return false;
  this->len_ = ::trackcodec::base64_encode(body, len, reinterpret_cast<char *>(this->body_.get()));
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

esp_err_t WebhookPoster::on_event_(esp_http_client_event_t *event) {
  if (event->event_id == HTTP_EVENT_ON_CONNECTED)
    static_cast<WebhookPoster *>(event->user_data)->connects_++;
  return ESP_OK;
}

int WebhookPoster::perform_() {
  this->connects_ = 0;
  int status = this->perform_once_();
  this->last_connects_.store(this->connects_);
  return status;
}

int WebhookPoster::perform_once_() {
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

bool WebhookTransport::setup(size_t batch_max) {
  // A new start begins at a random batch number, so Home Assistant never
  // mistakes the first batches after a restart for ones it has seen.
  this->sequence_ = static_cast<uint16_t>(random_uint32());
  if (!this->poster_.start(this->url_, sizeof(this->buf_))) {
    ESP_LOGE(TAG, "Could not start the webhook client");
    return false;
  }
  return true;
}

void WebhookTransport::dump_config() {
  // The URL itself stays out of the log: the webhook ID in it is the only key.
  ESP_LOGCONFIG(TAG,
                "  Protocol: compact batches to a Home Assistant webhook over HTTPS\n"
                "  Batches since boot: %" PRIu32 " over the kept connection, %" PRIu32 " over a new one, %" PRIu32
                " sent twice (the kept connection had closed)",
                this->batches_kept_, this->batches_new_, this->batches_resent_);
}

static ::trackcodec::CompactKind compact_kind(uint8_t decision) {
  switch (static_cast<::trackcodec::Decision>(decision)) {
    case ::trackcodec::Decision::START:
      return ::trackcodec::COMPACT_START;
    case ::trackcodec::Decision::STOP:
      return ::trackcodec::COMPACT_STOP;
    case ::trackcodec::Decision::HEARTBEAT:
      return ::trackcodec::COMPACT_HEARTBEAT;
    default:
      return ::trackcodec::COMPACT_FIX;
  }
}

size_t WebhookTransport::send(const RecordQueue &records, size_t max) {
  ::trackcodec::CompactEncoder enc;
  enc.begin(this->buf_, sizeof(this->buf_), this->sequence_, records.at(0).time);
  if (!this->identity_sent_)
    enc.set_identity(this->model_, this->firmware_);
  size_t count = 0;
  while (count < records.size() && count < max && enc.add(records.at(count), compact_kind(records.at(count).kind)))
    count++;
  if (count == 0 || !this->poster_.post(this->buf_, enc.size()))
    return 0;
  this->in_flight_ = count;
  this->identity_in_flight_ = enc.identity_added();
  const size_t sent = ::trackcodec::base64_size(enc.size());
  this->bytes_sent_ += sent;
  ESP_LOGD(TAG, "Posting %u records in %u bytes", (unsigned) count, (unsigned) sent);
  return count;
}

SendResult WebhookTransport::poll(uint32_t now) {
  int status = this->poster_.take_result();
  if (status == 0)
    return SendResult::PENDING;
  size_t n = this->in_flight_;
  this->in_flight_ = 0;
  this->sequence_++;
  if (status >= 200 && status < 300) {
    if (this->identity_in_flight_)
      this->identity_sent_ = true;
    this->note_connection_(now);
    return SendResult::ACKED;
  }
  if (status < 0) {
    ESP_LOGW(TAG, "No answer from Home Assistant (%s); %u records kept", esp_err_to_name(this->poster_.last_error()),
             (unsigned) n);
  } else if (status == 404 || status == 405) {
    ESP_LOGW(TAG, "Home Assistant doesn't know this webhook (HTTP %d): check the URL and the integration", status);
  } else {
    ESP_LOGW(TAG, "Home Assistant answered HTTP %d; %u records kept", status, (unsigned) n);
  }
  return SendResult::FAILED;
}

void WebhookTransport::note_connection_(uint32_t now) {
  // Whether Home Assistant (or Home Assistant Cloud) keeps the connection open
  // between batches decides much of each batch's cost on LTE: a new one costs a
  // TCP and TLS handshake, and one that closed while idle costs the request twice.
  const char *how;
  switch (this->poster_.last_connects()) {
    case 0:
      how = "kept connection";
      this->batches_kept_++;
      break;
    case 1:
      how = "new connection";
      this->batches_new_++;
      break;
    default:
      how = "sent twice: the kept connection had closed";
      this->batches_resent_++;
      break;
  }
  if (this->last_answer_ms_ == 0) {
    snprintf(this->ack_note_, sizeof(this->ack_note_), " (%s)", how);
  } else {
    snprintf(this->ack_note_, sizeof(this->ack_note_), " (%s, %" PRIu32 " s after the last)", how,
             (now - this->last_answer_ms_) / 1000);
  }
  this->last_answer_ms_ = now | 1;
}

}  // namespace esphome::tracker_uplink

#endif  // USE_UPLINK_WEBHOOK
