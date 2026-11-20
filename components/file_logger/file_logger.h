#pragma once

#include <atomic>
#include <cstdio>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

#include "log_line.h"

namespace esphome::file_logger {

/// Writes the device's log to a file (on an SD card, sd_card), so what happened
/// away from Wi-Fi can be read later. Lines wait in memory; a low-priority task
/// of its own writes them to the file, so a slow card never holds up the main
/// loop. When the file reaches max_size it becomes <path>.1 and a new one
/// starts, so the log takes at most twice max_size.
class FileLogger final : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  /// Before a planned restart or deep sleep: let the writer task write and sync what's waiting.
  void on_shutdown() override;
  // After the file systems are mounted.
  float get_setup_priority() const override { return setup_priority::IO; }

  void set_path(const char *path) { this->path_ = path; }
  void set_max_size(uint32_t bytes) { this->max_size_ = bytes; }
  void set_level(uint8_t level) { this->level_ = level; }
  void set_buffer_size(uint32_t bytes) { this->buffer_size_ = bytes; }
  void set_flush_interval(uint32_t ms) { this->flush_interval_ = ms; }

  void on_log(uint8_t level, const char *tag, const char *message, size_t message_len);

 protected:
  static void writer_task_(void *self);
  void write_loop_();
  bool open_();
  void rotate_();

  // Shared between the main loop (on_log) and the writer task.
  Mutex lock_;
  filelog::ByteRing ring_;
  uint32_t dropped_{0};

  // The writer task's own; read elsewhere only for status.
  FILE *file_{nullptr};
  std::atomic<bool> is_open_{false};
  std::atomic<bool> failed_{false};
  std::atomic<uint32_t> written_{0};
  std::atomic<bool> sync_now_{false};

  TaskHandle_t task_{nullptr};
  const char *path_{""};
  uint32_t max_size_{512 * 1024};
  uint32_t buffer_size_{4096};
  uint32_t flush_interval_{10000};
  uint8_t level_{7};
  bool warned_{false};
};

}  // namespace esphome::file_logger
