#pragma once
// Changes to a RingFile waiting for a writer task, so the loop that makes them
// never waits for the card: the loop (the one producer) adds pushes and pops,
// and the writer task (the one consumer) applies them to the file in order.
// size() and full() answer for the file as it will be once everything is
// written. No ESPHome dependencies: tests/cpp builds it on the host.

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>

namespace ringfile {

class PendingOps {
 public:
  /// Room for depth changes of record_size bytes, for a file holding count of
  /// capacity records. Call once, before the writer starts (from setup()).
  void init(uint16_t record_size, uint16_t depth, uint32_t count, uint32_t capacity) {
    this->record_size_ = record_size;
    this->depth_ = depth;
    this->count_ = count;
    this->capacity_ = capacity;
    this->pops_ = std::make_unique<uint32_t[]>(depth);  // NOLINT(cppcoreguidelines-avoid-c-arrays)
    this->records_ =
        std::make_unique<uint8_t[]>(size_t(depth) * record_size);  // NOLINT(cppcoreguidelines-avoid-c-arrays)
  }

  // The producer's side.

  /// Records the file holds once every waiting change is written.
  uint32_t size() const { return this->count_; }
  bool full() const { return this->count_ >= this->capacity_; }
  /// False when the queue is full, or there's no room for the change (the writer is behind).
  bool push(const void *record) {
    if (this->full() || !this->has_room_())
      return false;
    uint32_t head = this->head_.load(std::memory_order_relaxed);
    uint32_t slot = head % this->depth_;
    this->pops_[slot] = 0;
    memcpy(&this->records_[size_t(slot) * this->record_size_], record, this->record_size_);
    this->count_++;
    this->head_.store(head + 1, std::memory_order_release);
    return true;
  }
  /// Remove the n oldest records (at most all of them). False when there's no room for the change.
  bool pop(uint32_t n) {
    if (n > this->count_)
      n = this->count_;
    if (n == 0)
      return true;
    if (!this->has_room_())
      return false;
    uint32_t head = this->head_.load(std::memory_order_relaxed);
    this->pops_[head % this->depth_] = n;
    this->count_ -= n;
    this->head_.store(head + 1, std::memory_order_release);
    return true;
  }
  /// Whether every change has been applied to the file.
  bool idle() const {
    return this->tail_.load(std::memory_order_acquire) == this->head_.load(std::memory_order_acquire);
  }

  // The consumer's side.

  /// Apply every waiting change to file, oldest first. Returns how many were
  /// applied, or -1 when the file refused one (it stays waiting).
  template<typename File> int apply(File &file) {
    int applied = 0;
    uint32_t tail = this->tail_.load(std::memory_order_relaxed);
    while (tail != this->head_.load(std::memory_order_acquire)) {
      uint32_t slot = tail % this->depth_;
      uint32_t n = this->pops_[slot];
      bool ok = n == 0 ? file.push(&this->records_[size_t(slot) * this->record_size_]) : file.pop(n);
      if (!ok)
        return -1;
      tail++;
      this->tail_.store(tail, std::memory_order_release);
      applied++;
    }
    return applied;
  }

 protected:
  bool has_room_() const {
    return this->head_.load(std::memory_order_relaxed) - this->tail_.load(std::memory_order_acquire) < this->depth_;
  }

  std::unique_ptr<uint32_t[]> pops_;    // NOLINT(cppcoreguidelines-avoid-c-arrays): per slot, 0 for a push
  std::unique_ptr<uint8_t[]> records_;  // NOLINT(cppcoreguidelines-avoid-c-arrays)
  std::atomic<uint32_t> head_{0};       // the next slot to fill (producer)
  std::atomic<uint32_t> tail_{0};       // the next slot to apply (consumer)
  uint32_t count_{0};                   // producer only
  uint32_t capacity_{0};
  uint16_t record_size_{0};
  uint16_t depth_{1};
};

}  // namespace ringfile
