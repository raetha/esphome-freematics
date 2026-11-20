#pragma once

#include "esphome/core/component.h"

#include "ring_file.h"

namespace esphome::file_queue {

/// A first-in, first-out queue of fixed-size records in one file, kept across
/// reboots. The owning component sets the record layout at build time and
/// uses push(), read() and pop(); each call is a few small file writes. Changes
/// are synced to the card every sync_interval (a sync can take tens of ms).
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
  void set_record_layout(uint16_t size, uint16_t format) {
    this->record_size_ = size;
    this->record_format_ = format;
  }

  /// False when the file couldn't be opened or a write failed; the owner then
  /// keeps its records in memory only.
  bool is_ready() const { return this->file_.is_open() && !this->is_failed(); }
  uint32_t size() const { return this->file_.size(); }
  uint32_t capacity() const { return this->file_.capacity(); }
  bool full() const { return this->file_.full(); }

  /// Add a record of the registered size at the end.
  bool push(const void *record);
  /// Copy n records, from the index-th oldest, into out.
  bool read(uint32_t index, uint32_t n, void *out);
  /// Remove the n oldest records.
  bool pop(uint32_t n);

 protected:
  void io_failed_(const char *what);

  ringfile::RingFile file_;
  const char *path_{""};
  uint32_t capacity_{0};
  uint32_t sync_interval_{30000};
  uint32_t last_sync_{0};
  uint16_t record_size_{0};
  uint16_t record_format_{0};
};

}  // namespace esphome::file_queue
