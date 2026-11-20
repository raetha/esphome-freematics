#pragma once
// Plain pieces of file_logger, with no ESPHome dependencies (tests/cpp builds
// them on the host): a line cleaner and a byte ring for lines waiting to be
// written.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

namespace filelog {

/// Copy a log message into out without its colour codes, with what ESPHome
/// marks as secret (between ESC[8m and ESC[28m) replaced by "[redacted]".
/// Returns the length written; out is always terminated.
inline bool starts_with(const char *p, size_t avail, const char *marker, size_t marker_len) {
  return avail >= marker_len && memcmp(p, marker, marker_len) == 0;
}
inline bool is_letter(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

inline size_t clean_line(const char *msg, size_t len, char *out, size_t out_size) {
  static constexpr char SECRET_BEGIN[] = "\033[8m";
  static constexpr char SECRET_END[] = "\033[28m";
  static constexpr char REDACTED[] = "[redacted]";
  if (out_size == 0)
    return 0;
  size_t n = 0;
  auto put = [&](char c) {
    if (n + 1 < out_size)
      out[n++] = c;
  };
  size_t i = 0;
  while (i < len) {
    if (msg[i] != '\033') {
      put(msg[i++]);
      continue;
    }
    if (starts_with(msg + i, len - i, SECRET_BEGIN, sizeof(SECRET_BEGIN) - 1)) {
      for (const char *r = REDACTED; *r != '\0'; r++)
        put(*r);
      i += sizeof(SECRET_BEGIN) - 1;
      while (i < len && !starts_with(msg + i, len - i, SECRET_END, sizeof(SECRET_END) - 1))
        i++;
      i += i < len ? sizeof(SECRET_END) - 1 : 0;
      continue;
    }
    // An escape sequence: ESC '[' parameters, ending in a letter.
    i++;
    if (i < len && msg[i] == '[') {
      i++;
      while (i < len && !is_letter(msg[i]))
        i++;
      if (i < len)
        i++;
    }
  }
  out[n] = '\0';
  return n;
}

/// Bytes waiting to be written, in one buffer allocated once.
class ByteRing {
 public:
  void init(size_t capacity) {
    this->buf_ = std::make_unique<char[]>(capacity);  // NOLINT(cppcoreguidelines-avoid-c-arrays)
    this->capacity_ = capacity;
    this->head_ = 0;
    this->size_ = 0;
  }
  size_t size() const { return this->size_; }
  size_t capacity() const { return this->capacity_; }
  size_t free() const { return this->capacity_ - this->size_; }

  /// Add all of data, or nothing when it doesn't fit.
  bool push(const char *data, size_t len) {
    if (len > this->free())
      return false;
    for (size_t i = 0; i < len; i++)
      this->buf_[(this->head_ + this->size_ + i) % this->capacity_] = data[i];
    this->size_ += len;
    return true;
  }

  /// The oldest bytes that sit together in memory (fewer than size() when they wrap).
  size_t peek(const char **data) const {
    *data = this->buf_.get() + this->head_;
    size_t run = this->capacity_ - this->head_;
    return run < this->size_ ? run : this->size_;
  }

  void consume(size_t n) {
    if (n > this->size_)
      n = this->size_;
    this->head_ = this->capacity_ == 0 ? 0 : (this->head_ + n) % this->capacity_;
    this->size_ -= n;
  }

 protected:
  std::unique_ptr<char[]> buf_;  // NOLINT(cppcoreguidelines-avoid-c-arrays)
  size_t capacity_{0};
  size_t head_{0};
  size_t size_{0};
};

}  // namespace filelog
