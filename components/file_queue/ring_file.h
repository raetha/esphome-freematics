#pragma once
// A first-in, first-out queue of fixed-size records in one file, so it survives
// reboots and power loss. Plain C stdio with no ESPHome dependencies: the host
// tests (tests/cpp/ring_file_cli.cpp) build it with the system compiler.
//
// File layout: two 32-byte header slots, then `capacity` record slots used as a
// ring. A change writes the record first and then a header, with the next
// sequence number and a CRC, into the slot that doesn't hold the current one.
// Opening picks the valid header with the highest sequence number, so a write
// cut short by a reset leaves the previous state intact.

#include <cstdint>
#include <cstdio>
#include <cstring>

#include <unistd.h>

namespace ringfile {

static constexpr uint32_t MAGIC = 0x31515246;  // "FRQ1", little endian
static constexpr size_t HEADER_SIZE = 32;
static constexpr size_t DATA_OFFSET = 2 * HEADER_SIZE;

inline uint32_t crc32(const uint8_t *data, size_t len) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++)
      crc = (crc >> 1) ^ (0xEDB88320 & (0U - (crc & 1)));
  }
  return ~crc;
}

struct Header {
  uint32_t sequence{0};
  uint16_t record_size{0};
  uint16_t format{0};  // the owner's record layout version
  uint32_t capacity{0};
  uint32_t head{0};
  uint32_t count{0};
};

namespace detail {
inline void put32(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; i++)
    p[i] = (v >> (8 * i)) & 0xFF;
}
inline uint32_t get32(const uint8_t *p) {
  return p[0] | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

inline void encode(const Header &h, uint8_t *out) {
  memset(out, 0, HEADER_SIZE);
  put32(out, MAGIC);
  put32(out + 4, h.sequence);
  put32(out + 8, h.record_size | (uint32_t(h.format) << 16));
  put32(out + 12, h.capacity);
  put32(out + 16, h.head);
  put32(out + 20, h.count);
  put32(out + 28, crc32(out, 28));
}

/// False unless the slot holds a header with the right magic and CRC.
inline bool decode(const uint8_t *in, Header *h) {
  if (get32(in) != MAGIC || get32(in + 28) != crc32(in, 28))
    return false;
  h->sequence = get32(in + 4);
  uint32_t sizes = get32(in + 8);
  h->record_size = sizes & 0xFFFF;
  h->format = sizes >> 16;
  h->capacity = get32(in + 12);
  h->head = get32(in + 16);
  h->count = get32(in + 20);
  return h->capacity != 0 && h->record_size != 0 && h->head < h->capacity && h->count <= h->capacity;
}
}  // namespace detail

enum class OpenResult : uint8_t {
  LOADED,    // an existing queue, as it was
  CREATED,   // no file: a new, empty queue
  REPLACED,  // the file was unreadable or for other records: started empty
  FAILED,    // couldn't open or create the file
};

class RingFile {
 public:
  RingFile() = default;
  RingFile(const RingFile &) = delete;
  RingFile &operator=(const RingFile &) = delete;
  ~RingFile() { this->close(); }

  /// Open the queue at path, or create it, for records of record_size bytes.
  /// An existing file made for another record size, format or capacity is
  /// replaced by an empty queue.
  OpenResult open(const char *path, uint16_t record_size, uint16_t format, uint32_t capacity) {
    this->close();
    if (record_size == 0 || capacity == 0)
      return OpenResult::FAILED;
    Header want;
    want.record_size = record_size;
    want.format = format;
    want.capacity = capacity;

    this->file_ = fopen(path, "r+b");
    if (this->file_ != nullptr) {
      Header found;
      if (this->read_header_(&found) && found.record_size == record_size && found.format == format &&
          found.capacity == capacity) {
        this->header_ = found;
        return OpenResult::LOADED;
      }
      fclose(this->file_);
      this->file_ = fopen(path, "w+b");
      return this->start_(want) ? OpenResult::REPLACED : OpenResult::FAILED;
    }
    this->file_ = fopen(path, "w+b");
    return this->start_(want) ? OpenResult::CREATED : OpenResult::FAILED;
  }

  void close() {
    if (this->file_ != nullptr)
      fclose(this->file_);
    this->file_ = nullptr;
  }

  bool is_open() const { return this->file_ != nullptr; }
  uint32_t size() const { return this->header_.count; }
  uint32_t capacity() const { return this->header_.capacity; }
  bool empty() const { return this->header_.count == 0; }
  bool full() const { return this->header_.count >= this->header_.capacity; }

  /// Add a record at the end. False when full or on a write error.
  bool push(const void *record) {
    if (this->file_ == nullptr || this->full())
      return false;
    uint32_t slot = (this->header_.head + this->header_.count) % this->header_.capacity;
    if (!this->write_at_(this->offset_(slot), record, this->header_.record_size))
      return false;
    Header next = this->header_;
    next.count++;
    return this->commit_(next);
  }

  /// Copy n records, starting at the index-th oldest, into out.
  bool read(uint32_t index, uint32_t n, void *out) {
    if (this->file_ == nullptr || index > this->header_.count || n > this->header_.count - index)
      return false;
    auto *dst = static_cast<uint8_t *>(out);
    while (n > 0) {
      uint32_t slot = (this->header_.head + index) % this->header_.capacity;
      uint32_t run = this->header_.capacity - slot;  // records before the end of the ring
      if (run > n)
        run = n;
      size_t bytes = size_t(run) * this->header_.record_size;
      if (fseek(this->file_, this->offset_(slot), SEEK_SET) != 0 || fread(dst, 1, bytes, this->file_) != bytes)
        return false;
      dst += bytes;
      index += run;
      n -= run;
    }
    return true;
  }

  /// Remove the n oldest records.
  bool pop(uint32_t n) {
    if (this->file_ == nullptr)
      return false;
    if (n > this->header_.count)
      n = this->header_.count;
    if (n == 0)
      return true;
    Header next = this->header_;
    next.head = (next.head + n) % next.capacity;
    next.count -= n;
    if (next.count == 0)
      next.head = 0;
    return this->commit_(next);
  }

  bool clear() { return this->pop(this->header_.count); }

 protected:
  long offset_(uint32_t slot) const {  // NOLINT(google-runtime-int): fseek's type
    return static_cast<long>(DATA_OFFSET + size_t(slot) * this->header_.record_size);  // NOLINT(google-runtime-int)
  }

  bool write_at_(long offset, const void *data, size_t len) {  // NOLINT(google-runtime-int)
    return fseek(this->file_, offset, SEEK_SET) == 0 && fwrite(data, 1, len, this->file_) == len;
  }

  bool read_header_(Header *out) {
    uint8_t buf[2 * HEADER_SIZE];
    if (fseek(this->file_, 0, SEEK_SET) != 0 || fread(buf, 1, sizeof(buf), this->file_) != sizeof(buf))
      return false;
    Header a, b;
    bool va = detail::decode(buf, &a);
    bool vb = detail::decode(buf + HEADER_SIZE, &b);
    if (!va && !vb)
      return false;
    // Both valid: the newer one (sequence numbers wrap, so compare the difference).
    bool b_newer = vb && (!va || (b.sequence != a.sequence && b.sequence - a.sequence < 0x80000000U));
    *out = b_newer ? b : a;
    return true;
  }

  /// Write a fresh, empty queue into the just-truncated file.
  bool start_(const Header &want) {
    if (this->file_ == nullptr)
      return false;
    uint8_t zero[HEADER_SIZE] = {};
    if (!this->write_at_(HEADER_SIZE, zero, sizeof(zero))) {
      this->close();
      return false;
    }
    this->header_ = want;
    this->header_.sequence = 0;
    this->header_.head = 0;
    this->header_.count = 0;
    Header first = this->header_;
    if (!this->commit_(first)) {
      this->close();
      return false;
    }
    return true;
  }

  /// Write next as the new header, into the slot the current one isn't in.
  bool commit_(Header next) {
    next.sequence = this->header_.sequence + 1;
    uint8_t buf[HEADER_SIZE];
    detail::encode(next, buf);
    long slot = (next.sequence % 2) * HEADER_SIZE;  // NOLINT(google-runtime-int)
    if (!this->write_at_(slot, buf, sizeof(buf)) || fflush(this->file_) != 0 || fsync(fileno(this->file_)) != 0)
      return false;
    this->header_ = next;
    return true;
  }

  FILE *file_{nullptr};
  Header header_;
};

}  // namespace ringfile
