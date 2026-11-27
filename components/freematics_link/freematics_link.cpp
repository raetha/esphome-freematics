#include "freematics_link.h"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "esphome/core/application.h"
#include "esphome/core/log.h"

namespace esphome::freematics_link {

static const char *const TAG = "freematics_link";

// Timing uses the loop's start time, as ESPHome asks of components.
static uint32_t now_ms() { return App.get_loop_component_start_time(); }

static const char *command_text(Command cmd) {
  switch (cmd) {
    case Command::IDENTIFY:
      return "ATI\r";
    case Command::BATTERY:
      return "ATRV\r";
    case Command::GNSS_ON:
      return "ATGPSON\r";
    case Command::GNSS_READ:
      return "ATGPS\r";
    case Command::GNSS_OFF:
      return "ATGPSOFF\r";
    case Command::LOW_POWER:
      return "ATLP\r";
    default:
      return "";
  }
}

// For logs: the command without its "\r".
static int command_len(Command cmd) { return std::max<int>(0, static_cast<int>(strlen(command_text(cmd))) - 1); }

// How long to wait for a reply: the library's own timeouts, with room to spare.
static uint32_t command_timeout(Command cmd) { return cmd == Command::GNSS_READ ? 300 : 1000; }

void FreematicsLink::enter_standby() {
  if (this->state_ == State::STANDBY_GNSS_OFF || this->state_ == State::STANDBY_LOW_POWER ||
      this->state_ == State::STANDBY)
    return;
  ESP_LOGI(TAG, "Standby: GNSS off, co-processor to low power");
  this->state_ = State::STANDBY_GNSS_OFF;
}

void FreematicsLink::setup() {
  // In every log, the card's included, so it's clear which code a log came from.
  ESP_LOGI(TAG, "Firmware from raetha/esphome-freematics %s", this->source_);
  // The library resets co-processors from version 14 on with this pin; holding
  // it low briefly is harmless for older ones, which ignore it.
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->setup();
    this->reset_pin_->digital_write(false);
  }
  this->state_ = State::RESETTING;
  this->state_since_ = now_ms();
}

void FreematicsLink::dump_config() {
  ESP_LOGCONFIG(
      TAG,
      "Freematics co-processor link:\n"
      "  Firmware from raetha/esphome-freematics %s\n"
      "  GNSS read every %" PRIu32 " ms (0: off)\n"
      "  Battery voltage read every %" PRIu32 " ms (0: off)\n"
      "  Sends only ATI, ATRV, ATGPSON, ATGPS, and ATGPSOFF and ATLP before sleeping: nothing reaches the vehicle bus",
      this->source_, this->gnss_interval_, this->battery_interval_);
  LOG_PIN("  Reset pin: ", this->reset_pin_);
  // Shown whenever a log viewer connects, so it says how the link is doing now.
  if (this->version_number_ != 0) {
    ESP_LOGCONFIG(TAG, "  Co-processor version: %d.%d", this->version_number_ / 10, this->version_number_ % 10);
  } else {
    ESP_LOGCONFIG(TAG, "  Co-processor version: not identified");
  }
  ESP_LOGCONFIG(TAG,
                "  Bytes received since boot: %" PRIu32 "\n"
                "  GNSS replies: %" PRIu32 ", missed in a row: %u\n"
                "  GNSS restarted after %" PRIu32 " min without a fix: %u times",
                this->rx_total_, this->gnss_replies_, this->gnss_misses_, this->gnss_restart_ / 60000,
                this->gnss_restarts_);
  if (this->fix_now_) {
    ESP_LOGCONFIG(TAG, "  GNSS fix: %u satellites, %.5f, %.5f", this->last_fix_.satellites, this->last_fix_.latitude,
                  this->last_fix_.longitude);
  } else {
    ESP_LOGCONFIG(TAG, "  GNSS fix: none");
  }
}

void FreematicsLink::send_(Command cmd) {
  this->rx_len_ = 0;
  this->write_str(command_text(cmd));
  this->pending_ = cmd;
  this->sent_at_ = now_ms();
}

void FreematicsLink::loop() {
  uint32_t now = now_ms();

  // Collect what the co-processor sent, up to the "\r>" that ends a reply.
  while (this->available() > 0) {
    uint8_t c;
    if (!this->read_byte(&c))
      break;
    this->rx_total_++;
    if (this->rx_len_ < sizeof(this->rx_) - 1)
      this->rx_[this->rx_len_++] = static_cast<char>(c);
    this->rx_[this->rx_len_] = '\0';
    int end = ::freematics::reply_end(this->rx_, this->rx_len_);
    if (end >= 0) {
      this->rx_[end] = '\0';
      Command cmd = this->pending_;
      this->pending_ = Command::NONE;
      if (cmd == Command::GNSS_READ) {
        this->gnss_misses_ = 0;
        this->gnss_replies_++;
        ESP_LOGV(TAG, "GNSS: %s", this->rx_);  // every couple of seconds: verbose only
      } else {
        ESP_LOGD(TAG, "Reply to %.*s: %s", command_len(cmd), command_text(cmd), this->rx_);
      }
      this->handle_reply_(cmd, this->rx_);
      this->rx_len_ = 0;
    }
  }

  if (this->pending_ != Command::NONE) {
    if (now - this->sent_at_ < command_timeout(this->pending_))
      return;
    this->log_no_reply_();
    this->pending_ = Command::NONE;
    this->rx_len_ = 0;
  }

  switch (this->state_) {
    case State::RESETTING:
      if (this->reset_pin_ != nullptr && now - this->state_since_ >= 50)
        this->reset_pin_->digital_write(true);
      if (now - this->state_since_ >= 1000) {  // give the co-processor time to start
        // What arrived while it was in reset or starting is noise, not a reply.
        uint8_t c;
        while (this->available() > 0 && this->read_byte(&c)) {
        }
        this->rx_len_ = 0;
        this->state_ = State::IDENTIFYING;
        this->state_since_ = now;
      }
      return;
    case State::IDENTIFYING:
      if (this->version_number_ != 0 || this->identify_tries_ >= 3) {
        if (this->version_number_ == 0)
          ESP_LOGW(TAG, "The co-processor didn't identify itself; carrying on");
        this->state_ = this->gnss_interval_ != 0 ? State::GNSS_STARTING : State::RUNNING;
        return;
      }
      this->identify_tries_++;
      this->send_(Command::IDENTIFY);
      return;
    case State::GNSS_STARTING:
      this->send_(Command::GNSS_ON);
      this->gnss_since_ = now;
      this->state_ = State::RUNNING;
      return;
    case State::GNSS_RESTARTING:
      this->send_(Command::GNSS_OFF);
      this->state_ = State::GNSS_STARTING;
      return;
    case State::RUNNING:
      break;
    case State::STANDBY_GNSS_OFF:
      this->send_(Command::GNSS_OFF);
      this->state_ = State::STANDBY_LOW_POWER;
      return;
    case State::STANDBY_LOW_POWER:
      this->send_(Command::LOW_POWER);
      this->state_ = State::STANDBY;
      return;
    case State::STANDBY:
      return;
  }

  if (this->gnss_interval_ != 0 && this->gnss_restart_ != 0 && now - this->gnss_since_ >= this->gnss_restart_) {
    // Indoors this happens every gnss_restart without harm; outdoors it
    // recovers a receiver that stopped producing fixes.
    this->gnss_restarts_++;
    ESP_LOGW(TAG, "No GNSS fix for %" PRIu32 " min: switching the GNSS off and on again", this->gnss_restart_ / 60000);
    this->state_ = State::GNSS_RESTARTING;
    return;
  }
  if (this->gnss_interval_ != 0 && now - this->last_gnss_ >= this->gnss_interval_) {
    this->last_gnss_ = now;
    this->send_(Command::GNSS_READ);
    return;
  }
  if (this->battery_interval_ != 0 && now - this->last_battery_ >= this->battery_interval_) {
    this->last_battery_ = now;
    this->send_(Command::BATTERY);
  }
}

// A missing reply says a lot on the bench: whether anything at all came back
// (nothing: power, reset or wiring; bytes without the "\r>" prompt: the wrong
// baud rate or a different firmware). GNSS reads are polled, so only every
// 30th miss in a row is logged.
void FreematicsLink::log_no_reply_() {
  if (this->pending_ == Command::GNSS_READ && this->gnss_misses_++ % 30 != 0)
    return;
  // ATLP answers "OK" and goes to sleep before its prompt; sometimes it says
  // nothing at all. Neither is a fault: the ESP32 sleeps next anyway.
  if (this->pending_ == Command::LOW_POWER) {
    ESP_LOGD(TAG, "Co-processor in low power%s",
             this->rx_len_ >= 2 && this->rx_[0] == 'O' && this->rx_[1] == 'K' ? "" : " (no answer)");
    return;
  }
  char hex[3 * 16 + 1] = {};
  size_t n = std::min<size_t>(this->rx_len_, 16);
  for (size_t i = 0; i < n; i++)
    snprintf(hex + 3 * i, 4, "%02X ", static_cast<uint8_t>(this->rx_[i]));
  if (this->rx_len_ == 0) {
    ESP_LOGW(TAG, "No reply to %.*s (%" PRIu32 " bytes received since boot)", command_len(this->pending_),
             command_text(this->pending_), this->rx_total_);
  } else {
    ESP_LOGW(TAG, "Incomplete reply to %.*s: %u bytes, starting %s", command_len(this->pending_),
             command_text(this->pending_), (unsigned) this->rx_len_, hex);
  }
}

void FreematicsLink::handle_reply_(Command cmd, const char *reply) {
  switch (cmd) {
    case Command::IDENTIFY: {
      this->version_number_ = ::freematics::parse_version(reply);
      ESP_LOGI(TAG, "Co-processor: %s (version %d)", reply, this->version_number_);
#ifdef USE_TEXT_SENSOR
      if (this->version_ != nullptr && this->version_number_ != 0) {
        char buf[24];  // room for any int, so GCC sees nothing to truncate
        snprintf(buf, sizeof(buf), "%d.%d", this->version_number_ / 10, this->version_number_ % 10);
        this->version_->publish_state(buf);
      }
#endif
      break;
    }
    case Command::BATTERY: {
      float volts = ::freematics::parse_number(reply);
#ifdef USE_SENSOR
      if (this->battery_voltage_ != nullptr && !std::isnan(volts))
        this->battery_voltage_->publish_state(volts);
#endif
      break;
    }
    case Command::GNSS_READ:
      this->handle_fix_(reply);
      break;
    default:
      break;
  }
}

void FreematicsLink::handle_fix_(const char *reply) {
  Fix fix{};
  bool good = ::freematics::parse_gnifo(reply, &fix);
  if (good && this->has_fix_ && !::freematics::plausible_jump(this->last_fix_, fix)) {
    ESP_LOGD(TAG, "Ignoring a fix that jumped from the last one");
    good = false;
  }
#ifdef USE_BINARY_SENSOR
  if (this->fix_ != nullptr)
    this->fix_->publish_state(good);
#endif
  if (good != this->fix_now_) {
    if (good && !this->has_fix_) {
      // How long the first fix took is what placement and radio tests compare
      ESP_LOGI(TAG, "GNSS fix: %u satellites (the first, %" PRIu32 " s after start)", fix.satellites, now_ms() / 1000);
    } else if (good) {
      ESP_LOGI(TAG, "GNSS fix: %u satellites", fix.satellites);
    } else {
      ESP_LOGI(TAG, "GNSS fix lost");
    }
    this->fix_now_ = good;
  }
  if (!good)
    return;
  this->has_fix_ = true;
  this->last_fix_ = fix;
  this->gnss_since_ = now_ms();
#ifdef USE_SENSOR
  if (this->latitude_ != nullptr)
    this->latitude_->publish_state(fix.latitude);
  if (this->longitude_ != nullptr)
    this->longitude_->publish_state(fix.longitude);
  if (this->altitude_ != nullptr)
    this->altitude_->publish_state(fix.altitude_m);
  if (this->speed_ != nullptr)
    this->speed_->publish_state(fix.speed_kmh);
  if (this->course_ != nullptr)
    this->course_->publish_state(fix.course_deg);
  if (this->satellites_ != nullptr)
    this->satellites_->publish_state(fix.satellites);
  if (this->hdop_ != nullptr)
    this->hdop_->publish_state(fix.hdop);
#endif
  this->fix_trigger_.trigger(fix);
}

}  // namespace esphome::freematics_link
