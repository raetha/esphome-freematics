#pragma once

#include <cstdio>

#include "esphome/core/component.h"

#include "log_line.h"

namespace esphome::file_logger {

/// Writes the device's log to a file (on an SD card, sd_card), so what happened
/// away from Wi-Fi can be read later. Lines wait in memory and go to the file a
/// little at a time from loop(); when the file reaches max_size it becomes
/// <path>.1 and a new one starts, so the log takes at most twice max_size.
class FileLogger final : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  // After the file systems are mounted.
  float get_setup_priority() const override { return setup_priority::IO; }

  void set_path(const char *path) { this->path_ = path; }
  void set_max_size(uint32_t bytes) { this->max_size_ = bytes; }
  void set_level(uint8_t level) { this->level_ = level; }
  void set_buffer_size(uint32_t bytes) { this->buffer_size_ = bytes; }
  void set_flush_interval(uint32_t ms) { this->flush_interval_ = ms; }

  void on_log(uint8_t level, const char *tag, const char *message, size_t message_len);

 protected:
  bool open_();
  void rotate_();

  filelog::ByteRing ring_;
  FILE *file_{nullptr};
  const char *path_{""};
  uint32_t max_size_{512 * 1024};
  uint32_t buffer_size_{4096};
  uint32_t flush_interval_{10000};
  uint32_t last_flush_{0};
  uint32_t last_open_try_{0};
  uint32_t dropped_{0};
  uint32_t written_{0};
  bool unsynced_{false};
  uint8_t level_{7};
};

}  // namespace esphome::file_logger
