#pragma once
// A fixed-capacity FIFO of records, allocated once (ESPHome components avoid
// heap allocation after setup()). No ESPHome dependencies (tests/cpp).

#include <cstddef>
#include <memory>

#include "teltonika.h"

namespace trackcodec {

class RecordQueue {
 public:
  /// Allocate room for capacity records. Call once, from setup().
  void init(size_t capacity) {
    this->buf_ = std::make_unique<Record[]>(capacity);  // NOLINT(cppcoreguidelines-avoid-c-arrays)
    this->capacity_ = capacity;
    this->head_ = 0;
    this->size_ = 0;
  }

  size_t size() const { return this->size_; }
  size_t capacity() const { return this->capacity_; }
  bool empty() const { return this->size_ == 0; }
  bool full() const { return this->size_ == this->capacity_; }

  /// The i-th oldest record.
  const Record &at(size_t i) const { return this->buf_[(this->head_ + i) % this->capacity_]; }

  /// Add at the end. Returns false (and adds nothing) when full.
  bool push_back(const Record &r) {
    if (this->full())
      return false;
    this->buf_[(this->head_ + this->size_) % this->capacity_] = r;
    this->size_++;
    return true;
  }

  /// Remove the n oldest records.
  void pop_front(size_t n) {
    if (n > this->size_)
      n = this->size_;
    if (this->capacity_ != 0)
      this->head_ = (this->head_ + n) % this->capacity_;
    this->size_ -= n;
  }

  /// Remove the i-th oldest record, keeping the order of the rest.
  void erase(size_t i) {
    if (i >= this->size_)
      return;
    for (size_t j = i; j + 1 < this->size_; j++)
      this->slot_(j) = this->at(j + 1);
    this->size_--;
  }

  /// Copy records [first, first + n) into out, in order.
  void copy(size_t first, size_t n, Record *out) const {
    for (size_t i = 0; i < n; i++)
      out[i] = this->at(first + i);
  }

 protected:
  Record &slot_(size_t i) { return this->buf_[(this->head_ + i) % this->capacity_]; }

  std::unique_ptr<Record[]> buf_;  // NOLINT(cppcoreguidelines-avoid-c-arrays)
  size_t capacity_{0};
  size_t head_{0};
  size_t size_{0};
};

}  // namespace trackcodec
