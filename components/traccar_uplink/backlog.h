#pragma once
// The records waiting for Traccar's acknowledgement: in memory and, optionally,
// in a store that survives restarts (a file on an SD card, file_queue). With a
// store, the store holds every waiting record and memory holds the oldest of
// them, the next to send; without one, memory is all there is. If the store
// fails, memory carries on alone.
//
// No ESPHome dependencies: tests/cpp builds it on the host. A Store has
// is_ready(), size(), full(), push(const void *), read(index, n, void *) and pop(n).

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "record_queue.h"

namespace trackcodec {

enum class Added : uint8_t {
  OK,
  DROPPED_OLDEST,  // full: the oldest record not in flight made room
  DROPPED,         // full of records in flight: this one was dropped
};

/// The store type when there's none.
struct NoStore {
  bool is_ready() const { return false; }
  uint32_t size() const { return 0; }
  bool full() const { return true; }
  bool push(const void * /*record*/) { return false; }
  bool read(uint32_t /*index*/, uint32_t /*n*/, void * /*out*/) { return false; }
  bool pop(uint32_t /*n*/) { return false; }
};

template<typename Store> class Backlog {
 public:
  /// Allocate memory_capacity records (call once, from setup()) and load what
  /// the store kept from before.
  void init(size_t memory_capacity, Store *store) {
    this->memory_.init(memory_capacity);
    this->store_ = store;
    this->refill_();
  }

  bool stored() const { return this->store_ != nullptr && this->store_->is_ready(); }
  /// Every waiting record.
  size_t size() const { return this->stored() ? this->store_->size() : this->memory_.size(); }
  bool empty() const { return this->size() == 0; }
  /// The oldest waiting records, the next to send.
  const RecordQueue &memory() const { return this->memory_; }

  /// Add a record at the end. The in_flight oldest records are in a packet
  /// waiting for its answer and are never dropped to make room.
  Added add(const Record &record, size_t in_flight) {
    Added result = Added::OK;
    if (this->stored()) {
      if (this->store_->full()) {
        if (in_flight != 0)
          return Added::DROPPED;
        this->store_->pop(1);
        this->memory_.pop_front(1);
        result = Added::DROPPED_OLDEST;
      }
      bool holds_all = this->memory_.size() == this->store_->size();
      if (this->store_->push(&record)) {
        if (holds_all)
          this->memory_.push_back(record);  // when memory is full, it waits in the store
        else
          this->refill_();  // memory has room after dropping the oldest
        return result;
      }
      // The store failed: memory only from here on.
    }
    if (this->memory_.full()) {
      if (this->memory_.size() <= in_flight)
        return Added::DROPPED;
      this->memory_.erase(in_flight);
      result = Added::DROPPED_OLDEST;
    }
    this->memory_.push_back(record);
    return result;
  }

  /// Traccar acknowledged the n oldest records.
  void acknowledged(size_t n) {
    n = std::min(n, this->memory_.size());
    this->memory_.pop_front(n);
    if (this->stored()) {
      this->store_->pop(n);
      this->refill_();
    }
  }

 protected:
  /// Copy the records after the ones in memory from the store, while there's room.
  void refill_() {
    static constexpr size_t CHUNK = 8;
    Record chunk[CHUNK];  // NOLINT(cppcoreguidelines-avoid-c-arrays)
    while (this->stored() && this->memory_.size() < this->store_->size() && !this->memory_.full()) {
      size_t n = std::min({static_cast<size_t>(this->store_->size()) - this->memory_.size(),
                           this->memory_.capacity() - this->memory_.size(), CHUNK});
      if (!this->store_->read(this->memory_.size(), n, chunk))
        return;
      for (size_t i = 0; i < n; i++)
        this->memory_.push_back(chunk[i]);
    }
  }

  RecordQueue memory_;
  Store *store_{nullptr};
};

}  // namespace trackcodec
