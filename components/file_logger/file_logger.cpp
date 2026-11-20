#include "file_logger.h"

#include <cinttypes>
#include <ctime>

#include <unistd.h>

#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "esphome/components/logger/logger.h"

namespace esphome::file_logger {

static const char *const TAG = "file_logger";

// Each loop() writes at most this much, so a slow card never holds the loop up long.
static constexpr size_t WRITE_CHUNK = 512;
// Before a clock is set, lines carry the uptime instead of the date.
static constexpr time_t CLOCK_SET = 1700000000;

static uint32_t now_ms() { return App.get_loop_component_start_time(); }

void FileLogger::setup() {
  this->ring_.init(this->buffer_size_);
  this->open_();
  if (logger::global_logger != nullptr) {
    logger::global_logger->add_log_callback(
        this, [](void *self, uint8_t level, const char *tag, const char *message, size_t message_len) {
          static_cast<FileLogger *>(self)->on_log(level, tag, message, message_len);
        });
  }
}

bool FileLogger::open_() {
  this->last_open_try_ = now_ms();
  this->file_ = fopen(this->path_, "a");
  if (this->file_ == nullptr) {
    this->status_set_warning(LOG_STR("Can't open the log file (no card?)"));
    return false;
  }
  this->status_clear_warning();
  return true;
}

void FileLogger::on_log(uint8_t level, const char *tag, const char *message, size_t message_len) {
  if (level > this->level_)
    return;
  // Called from the main loop only (the logger hands other tasks' lines over to
  // it), so this needs no lock. It must not log.
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
  if (this->dropped_ != 0) {
    char note[48];
    int m = snprintf(note, sizeof(note), "[%" PRIu32 " log lines dropped]\n", this->dropped_);
    if (m > 0 && this->ring_.push(note, m))
      this->dropped_ = 0;
  }
  if (this->dropped_ != 0 || !this->ring_.push(line, n))
    this->dropped_++;
}

void FileLogger::loop() {
  uint32_t now = now_ms();
  if (this->file_ == nullptr) {
    if (now - this->last_open_try_ < 60000 || !this->open_())
      return;
  }
  bool due = this->ring_.size() > this->ring_.capacity() / 2 || now - this->last_flush_ >= this->flush_interval_;
  if (this->ring_.size() != 0 && due) {
    const char *data;
    size_t n = this->ring_.peek(&data);
    if (n > WRITE_CHUNK)
      n = WRITE_CHUNK;
    if (fwrite(data, 1, n, this->file_) != n) {
      fclose(this->file_);
      this->file_ = nullptr;
      this->status_set_warning(LOG_STR("Writing the log file failed"));
      return;
    }
    this->ring_.consume(n);
    this->written_ += n;
    this->unsynced_ = true;
    return;  // the next chunk on the next loop
  }
  if (this->unsynced_ && this->ring_.size() == 0) {
    // Everything waiting is written: make it survive a power cut.
    fflush(this->file_);
    fsync(fileno(this->file_));
    this->unsynced_ = false;
    this->last_flush_ = now;
    if (ftell(this->file_) >= static_cast<long>(this->max_size_))  // NOLINT(google-runtime-int)
      this->rotate_();
  }
}

void FileLogger::rotate_() {
  fclose(this->file_);
  this->file_ = nullptr;
  char old[64];
  snprintf(old, sizeof(old), "%s.1", this->path_);
  remove(old);
  rename(this->path_, old);
  this->open_();
}

void FileLogger::dump_config() {
  ESP_LOGCONFIG(TAG,
                "File logger:\n"
                "  Path: %s (%s)\n"
                "  Max size: %" PRIu32 " bytes, then %s.1\n"
                "  Written since boot: %" PRIu32 " bytes; dropped lines waiting: %" PRIu32,
                this->path_, this->file_ != nullptr ? "open" : "not open", this->max_size_, this->path_, this->written_,
                this->dropped_);
}

}  // namespace esphome::file_logger
