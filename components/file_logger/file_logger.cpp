#include "file_logger.h"

#ifdef USE_FILE_LOGGER_HTTP
#include "esphome/components/network/util.h"
#endif

#include <algorithm>
#include <cinttypes>
#include <cstring>
#include <ctime>

#include <unistd.h>

#include "esphome/core/log.h"
#include "esphome/components/logger/logger.h"

namespace esphome::file_logger {

static const char *const TAG = "file_logger";

// The writer task writes at most this much at a time, outside the lock.
static constexpr size_t WRITE_CHUNK = 512;
// How long the writer task sleeps when there's nothing to write.
static constexpr TickType_t IDLE_WAIT = pdMS_TO_TICKS(200);
// Before a clock is set, lines carry the uptime instead of the date.
static constexpr time_t CLOCK_SET = 1700000000;
#ifdef USE_FILE_LOGGER_HTTP
static constexpr size_t DOWNLOAD_CHUNK = 2048;
#endif

void FileLogger::setup() {
  this->ring_.init(this->buffer_size_);
  if (logger::global_logger != nullptr) {
    logger::global_logger->add_log_callback(
        this, [](void *self, uint8_t level, const char *tag, const char *message, size_t message_len) {
          static_cast<FileLogger *>(self)->on_log(level, tag, message, message_len);
        });
  }
#ifdef USE_FILE_LOGGER_HTTP
  if (this->web_server_ != nullptr) {
    this->chunk_ = std::make_unique<char[]>(DOWNLOAD_CHUNK);  // NOLINT(cppcoreguidelines-avoid-c-arrays)
    this->web_server_->add_handler(&this->handler_);
    // The HTTP server opens a socket, which needs the network stack: this
    // component starts before the network does (to log its start), so the
    // server starts once there's a network.
    this->set_interval("http", 1000, [this]() {
      if (!network::is_connected())
        return;
      this->web_server_->init();
      this->cancel_interval("http");
    });
  }
#endif
  // Below the main loop's priority, on either core: it only drains the buffer.
  if (xTaskCreate(writer_task_, "file_logger", 4096, this, 1, &this->task_) != pdPASS)
    this->mark_failed(LOG_STR("Couldn't start the writer task"));
}

void FileLogger::on_log(uint8_t level, const char *tag, const char *message, size_t message_len) {
  if (level > this->level_)
    return;
  // Called from the main loop only (the logger hands other tasks' lines over to
  // it). It must not log.
  char line[320];
  size_t n = 0;
  time_t t = ::time(nullptr);
  if (t >= CLOCK_SET) {
    struct tm tm;
    gmtime_r(&t, &tm);
    n = strftime(line, sizeof(line), "%Y-%m-%d %H:%M:%SZ ", &tm);
  } else {
    n = snprintf(line, sizeof(line), "+%" PRIu32 "s ", millis() / 1000);
  }
  n += filelog::clean_line(message, message_len, line + n, sizeof(line) - n - 1);
  line[n++] = '\n';

  LockGuard guard(this->lock_);
  if (this->dropped_ != 0) {
    char note[48];
    int m = snprintf(note, sizeof(note), "[%" PRIu32 " log lines dropped]\n", this->dropped_);
    if (m > 0 && this->ring_.push(note, m))
      this->dropped_ = 0;
  }
  if (this->dropped_ != 0 || !this->ring_.push(line, n))
    this->dropped_++;
}

// The main loop only reports the writer task's state.
void FileLogger::loop() {
  bool trouble = !this->is_open_.load() || this->failed_.load();
  if (trouble && !this->warned_) {
    this->status_set_warning(LOG_STR("Can't write the log file (no card?)"));
    this->warned_ = true;
  } else if (!trouble && this->warned_) {
    this->status_clear_warning();
    this->warned_ = false;
  }
}

void FileLogger::writer_task_(void *self) { static_cast<FileLogger *>(self)->write_loop_(); }

bool FileLogger::open_() {
  this->file_ = fopen(this->path_, "a");
  this->is_open_.store(this->file_ != nullptr);
  return this->file_ != nullptr;
}

void FileLogger::on_shutdown() {
  if (this->file_ == nullptr)
    return;
  // The writer task writes what's waiting and syncs it; wait up to half a
  // second for it (shutdown is the one place a short wait is acceptable).
  this->sync_now_.store(true);
  for (int i = 0; i < 50 && this->sync_now_.load(); i++)
    vTaskDelay(pdMS_TO_TICKS(10));
}

void FileLogger::write_loop_() {
  char chunk[WRITE_CHUNK];
  TickType_t last_open_try = 0;
  TickType_t last_sync = xTaskGetTickCount();
  bool unsynced = false;
  for (;;) {
    TickType_t now = xTaskGetTickCount();
    if (this->file_ == nullptr) {
      // No card, or it failed: try again every minute.
      if ((last_open_try != 0 && now - last_open_try < pdMS_TO_TICKS(60000)) || !this->open_()) {
        last_open_try = now == 0 ? 1 : now;
        vTaskDelay(pdMS_TO_TICKS(1000));
        continue;
      }
      this->failed_.store(false);
    }

    size_t n = 0;
    {
      LockGuard guard(this->lock_);
      const char *data;
      n = std::min(this->ring_.peek(&data), WRITE_CHUNK);
      memcpy(chunk, data, n);
      this->ring_.consume(n);
    }
    if (n != 0) {
      if (fwrite(chunk, 1, n, this->file_) != n) {
        fclose(this->file_);
        this->file_ = nullptr;
        this->is_open_.store(false);
        this->failed_.store(true);
        continue;
      }
      this->written_.fetch_add(n);
      unsynced = true;
      continue;  // more may be waiting
    }

    if (!unsynced)
      this->sync_now_.store(false);  // nothing waiting: a shutdown needn't wait
    if (unsynced && (this->sync_now_.load() || now - last_sync >= pdMS_TO_TICKS(this->flush_interval_))) {
      // Everything waiting is written: make it survive a power cut.
      fflush(this->file_);
      fsync(fileno(this->file_));
      unsynced = false;
      last_sync = now;
      this->sync_now_.store(false);
      if (ftell(this->file_) >= static_cast<long>(this->max_size_))  // NOLINT(google-runtime-int)
        this->rotate_();
    }
    vTaskDelay(IDLE_WAIT);
  }
}

void FileLogger::rotate_() {
  LockGuard guard(this->file_lock_);  // not while a download reads the file
  fclose(this->file_);
  this->file_ = nullptr;
  char old[72];
  snprintf(old, sizeof(old), "%s.1", this->path_);
  remove(old);
  rename(this->path_, old);
  this->open_();
}

void FileLogger::dump_config() {
  uint32_t dropped;
  {
    LockGuard guard(this->lock_);
    dropped = this->dropped_;
  }
  ESP_LOGCONFIG(TAG,
                "File logger:\n"
                "  Path: %s (%s)\n"
                "  Max size: %" PRIu32 " bytes, then %s.1\n"
                "  Written since boot: %" PRIu32 " bytes; dropped lines waiting: %" PRIu32,
                this->path_, this->is_open_.load() ? "open" : "not open", this->max_size_, this->path_,
                this->written_.load(), dropped);
#ifdef USE_FILE_LOGGER_HTTP
  if (this->web_server_ != nullptr)
    ESP_LOGCONFIG(TAG, "  Download: http://<device>:%u/log and /log.1, with the web server's password",
                  this->web_server_->get_port());
#endif
}

#ifdef USE_FILE_LOGGER_HTTP
bool LogDownloadHandler::canHandle(AsyncWebServerRequest *request) const {
  if (request->method() != HTTP_GET)
    return false;
  char buf[AsyncWebServerRequest::URL_BUF_SIZE];
  StringRef url = request->url_to(buf);
  return url == "/log" || url == "/log.1";
}

void LogDownloadHandler::handleRequest(AsyncWebServerRequest *request) {
  char buf[AsyncWebServerRequest::URL_BUF_SIZE];
  bool old = request->url_to(buf) == "/log.1";
  if (!this->parent_->send_file(*request, old))
    request->send(503, "text/plain", "Another download is under way");
}

bool FileLogger::send_file(httpd_req_t *req, bool old) {
  bool expected = false;
  if (!this->sending_.compare_exchange_strong(expected, true))
    return false;
  char path[72];
  snprintf(path, sizeof(path), old ? "%s.1" : "%s", this->path_);
  // What's waiting goes to the file first (by the next write, within a moment).
  this->sync_now_.store(true);
  {
    LockGuard guard(this->file_lock_);
    FILE *f = fopen(path, "r");
    if (f == nullptr) {
      httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "No such log file");
    } else {
      httpd_resp_set_type(req, "text/plain; charset=utf-8");
      const char *name = old ? "attachment; filename=\"esphome.log.1\"" : "attachment; filename=\"esphome.log\"";
      httpd_resp_set_hdr(req, "Content-Disposition", name);
      size_t n;
      size_t total = 0;
      bool ok = true;
      while (ok && (n = fread(this->chunk_.get(), 1, DOWNLOAD_CHUNK, f)) > 0) {
        ok = httpd_resp_send_chunk(req, this->chunk_.get(), static_cast<ssize_t>(n)) == ESP_OK;
        total += n;
      }
      fclose(f);
      httpd_resp_send_chunk(req, nullptr, 0);
      ESP_LOGI(TAG, "Sent %s over HTTP (%u bytes%s)", path, (unsigned) total, ok ? "" : ", cut short");
    }
  }
  this->sending_.store(false);
  return true;
}
#endif

}  // namespace esphome::file_logger
