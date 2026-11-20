#include "file_queue.h"

#include "esphome/core/application.h"
#include "esphome/core/log.h"

namespace esphome::file_queue {

static const char *const TAG = "file_queue";

void FileQueue::setup() {
  if (this->record_size_ == 0) {
    this->mark_failed(LOG_STR("No component keeps records in this queue"));
    return;
  }
  switch (this->file_.open(this->path_, this->record_size_, this->record_format_, this->capacity_)) {
    case ringfile::OpenResult::LOADED:
      ESP_LOGI(TAG, "%s: %u records kept from before", this->path_, (unsigned) this->file_.size());
      break;
    case ringfile::OpenResult::CREATED:
      ESP_LOGI(TAG, "%s: created", this->path_);
      break;
    case ringfile::OpenResult::REPLACED:
      ESP_LOGW(TAG, "%s: unreadable or for other records; started empty", this->path_);
      break;
    case ringfile::OpenResult::FAILED:
      ESP_LOGW(TAG, "%s: can't open (no card?); records stay in memory", this->path_);
      this->mark_failed();
      break;
  }
}

void FileQueue::loop() {
  uint32_t now = App.get_loop_component_start_time();
  if (!this->is_ready() || !this->file_.dirty() || now - this->last_sync_ < this->sync_interval_)
    return;
  this->last_sync_ = now;
  if (!this->file_.sync())
    this->io_failed_("sync");
}

void FileQueue::dump_config() {
  ESP_LOGCONFIG(TAG,
                "File queue:\n"
                "  Path: %s\n"
                "  Capacity: %u records of %u bytes, synced every %u s\n"
                "  Held: %u",
                this->path_, (unsigned) this->capacity_, this->record_size_, (unsigned) (this->sync_interval_ / 1000),
                (unsigned) this->file_.size());
}

void FileQueue::io_failed_(const char *what) {
  ESP_LOGE(TAG, "%s: %s failed; records stay in memory until restart", this->path_, what);
  this->file_.close();
  this->mark_failed();
}

bool FileQueue::push(const void *record) {
  if (!this->is_ready() || this->file_.full())
    return false;
  if (this->file_.push(record))
    return true;
  this->io_failed_("write");
  return false;
}

bool FileQueue::read(uint32_t index, uint32_t n, void *out) {
  if (!this->is_ready() || index > this->file_.size() || n > this->file_.size() - index)
    return false;
  if (this->file_.read(index, n, out))
    return true;
  this->io_failed_("read");
  return false;
}

bool FileQueue::pop(uint32_t n) {
  if (!this->is_ready())
    return false;
  if (this->file_.pop(n))
    return true;
  this->io_failed_("write");
  return false;
}

}  // namespace esphome::file_queue
