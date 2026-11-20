#include "file_queue.h"

#include "esphome/core/application.h"
#include "esphome/core/log.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

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
      return;
  }
  if (!this->use_task_)
    return;
  // Room for a few minutes of records while the card is slow; a full queue makes
  // push() wait briefly, then fail (the owner carries on in memory).
  static constexpr uint16_t DEPTH = 32;
  this->pending_.init(this->record_size_, DEPTH, this->file_.size(), this->file_.capacity());
  if (xTaskCreate(writer_task_, "file_queue", 4096, this, 1, &this->task_) != pdPASS) {
    this->task_ = nullptr;
    ESP_LOGW(TAG, "No writer task; writing from the loop");
  }
}

void FileQueue::writer_task_(void *self) { static_cast<FileQueue *>(self)->write_loop_(); }

void FileQueue::write_loop_() {
  TickType_t last_sync = xTaskGetTickCount();
  for (;;) {
    // Woken by each change, or once a second to keep the sync on time.
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
    if (this->task_failed_.load())
      continue;
    LockGuard guard(this->file_lock_);
    if (this->pending_.apply(this->file_) < 0) {
      this->task_error_ = "write";
      this->task_failed_.store(true);
      continue;
    }
    TickType_t now = xTaskGetTickCount();
    bool due = now - last_sync >= pdMS_TO_TICKS(this->sync_interval_);
    if (this->file_.dirty() && (due || this->sync_now_.load())) {
      if (!this->file_.sync()) {
        this->task_error_ = "sync";
        this->task_failed_.store(true);
        continue;
      }
      last_sync = now;
    }
    this->sync_now_.store(false);
  }
}

bool FileQueue::wait_for_writer_(uint32_t timeout_ms) {
  for (uint32_t waited = 0; !this->pending_.idle(); waited += 10) {
    if (waited >= timeout_ms || this->task_failed_.load())
      return false;
    xTaskNotifyGive(this->task_);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return true;
}

void FileQueue::loop() {
  if (this->task_ != nullptr) {
    if (this->task_failed_.load() && !this->reported_) {
      this->reported_ = true;
      ESP_LOGE(TAG, "%s: %s failed; records stay in memory until restart", this->path_, this->task_error_);
      this->mark_failed();
    }
    return;
  }
  uint32_t now = App.get_loop_component_start_time();
  if (!this->is_ready() || !this->file_.dirty() || now - this->last_sync_ < this->sync_interval_)
    return;
  this->last_sync_ = now;
  if (!this->file_.sync())
    this->io_failed_("sync");
}

void FileQueue::on_shutdown() {
  if (this->task_ != nullptr) {
    // The writer writes what's waiting and syncs it; give it up to a second
    // (shutdown is the one place a short wait is acceptable).
    this->sync_now_.store(true);
    xTaskNotifyGive(this->task_);
    for (int i = 0; i < 100 && (this->sync_now_.load() || !this->pending_.idle()) && !this->task_failed_.load(); i++)
      vTaskDelay(pdMS_TO_TICKS(10));
    return;
  }
  if (this->is_ready() && this->file_.dirty() && !this->file_.sync())
    this->io_failed_("sync");
}

void FileQueue::dump_config() {
  ESP_LOGCONFIG(TAG,
                "File queue:\n"
                "  Path: %s\n"
                "  Capacity: %u records of %u bytes, synced every %u s\n"
                "  Written by: %s\n"
                "  Held: %u",
                this->path_, (unsigned) this->capacity_, this->record_size_, (unsigned) (this->sync_interval_ / 1000),
                this->task_ != nullptr ? "a task of its own" : "the loop", (unsigned) this->size());
}

void FileQueue::io_failed_(const char *what) {
  ESP_LOGE(TAG, "%s: %s failed; records stay in memory until restart", this->path_, what);
  this->file_.close();
  this->mark_failed();
}

bool FileQueue::push(const void *record) {
  if (!this->is_ready() || this->full())
    return false;
  if (this->task_ != nullptr) {
    // The writer is far behind only when the card is: wait for room briefly.
    for (int i = 0; !this->pending_.push(record); i++) {
      if (i >= 50 || this->task_failed_.load())
        return false;
      xTaskNotifyGive(this->task_);
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    xTaskNotifyGive(this->task_);
    return true;
  }
  if (this->file_.push(record))
    return true;
  this->io_failed_("write");
  return false;
}

bool FileQueue::read(uint32_t index, uint32_t n, void *out) {
  if (!this->is_ready() || index > this->size() || n > this->size() - index)
    return false;
  if (this->task_ != nullptr) {
    // Only after a long outage (more waiting than the owner keeps in memory).
    if (!this->wait_for_writer_(2000))
      return false;
    LockGuard guard(this->file_lock_);
    if (this->file_.read(index, n, out))
      return true;
    this->task_error_ = "read";
    this->task_failed_.store(true);
    return false;
  }
  if (this->file_.read(index, n, out))
    return true;
  this->io_failed_("read");
  return false;
}

bool FileQueue::pop(uint32_t n) {
  if (!this->is_ready())
    return false;
  if (this->task_ != nullptr) {
    for (int i = 0; !this->pending_.pop(n); i++) {
      if (i >= 50 || this->task_failed_.load())
        return false;
      xTaskNotifyGive(this->task_);
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    xTaskNotifyGive(this->task_);
    return true;
  }
  if (this->file_.pop(n))
    return true;
  this->io_failed_("write");
  return false;
}

}  // namespace esphome::file_queue
