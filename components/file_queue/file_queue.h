#pragma once

#include <atomic>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

#include "pending_ops.h"
#include "ring_file.h"

namespace esphome::file_queue {

/// A first-in, first-out queue of fixed-size records in one file, kept across
/// reboots. The owning component sets the record layout at build time and
/// uses push(), read() and pop(). With a writer task (the default), push() and
/// pop() only queue the change and a task of its own writes it to the card and
/// syncs every sync_interval, so the loop never waits for the card; read()
/// waits for the writer to catch up (owners read only after a long outage).
/// Without one, each call writes to the card from the loop.
class FileQueue final : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  /// Before a planned restart or deep sleep: sync what isn't on the card yet.
  void on_shutdown() override;
  // After file systems are mounted (sd_card), before the components using the queue.
  float get_setup_priority() const override { return setup_priority::IO; }

  void set_path(const char *path) { this->path_ = path; }
  void set_capacity(uint32_t capacity) { this->capacity_ = capacity; }
  void set_sync_interval(uint32_t ms) { this->sync_interval_ = ms; }
  void set_writer_task(bool on) { this->use_task_ = on; }
  void set_record_layout(uint16_t size, uint16_t format) {
    this->record_size_ = size;
    this->record_format_ = format;
  }

  /// False when the file couldn't be opened or a write failed; the owner then
  /// keeps its records in memory only.
  bool is_ready() const { return this->file_.is_open() && !this->is_failed() && !this->task_failed_.load(); }
  uint32_t size() const { return this->task_ != nullptr ? this->pending_.size() : this->file_.size(); }
  uint32_t capacity() const { return this->file_.capacity(); }
  bool full() const { return this->task_ != nullptr ? this->pending_.full() : this->file_.full(); }

  /// Add a record of the registered size at the end.
  bool push(const void *record);
  /// Copy n records, from the index-th oldest, into out.
  bool read(uint32_t index, uint32_t n, void *out);
  /// Remove the n oldest records.
  bool pop(uint32_t n);

 protected:
  void io_failed_(const char *what);
  static void writer_task_(void *self);
  void write_loop_();
  /// Wait (a bounded time) for the writer to apply every queued change.
  bool wait_for_writer_(uint32_t timeout_ms);

  ringfile::RingFile file_;
  const char *path_{""};
  uint32_t capacity_{0};
  uint32_t sync_interval_{30000};
  uint32_t last_sync_{0};
  uint16_t record_size_{0};
  uint16_t record_format_{0};

  // With a writer task: the changes waiting for it, and the file shared with it.
  bool use_task_{true};
  ringfile::PendingOps pending_;
  Mutex file_lock_;  // held by the writer while it writes, and by read()
  TaskHandle_t task_{nullptr};
  std::atomic<bool> task_failed_{false};
  std::atomic<bool> sync_now_{false};
  const char *task_error_{""};
  bool reported_{false};
};

}  // namespace esphome::file_queue
