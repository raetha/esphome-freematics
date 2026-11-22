#pragma once
// The compact batch the Home Assistant integration (raetha/ha-freematics)
// takes through a webhook: each record carries only what changed since the
// one before (time in seconds, position in millionths of a degree, altitude in
// metres), so a record while driving is about 11 bytes against GH3000's 19.
//
//   batch:  version (1), sequence (uint16 LE), base time (uint32 LE, Unix s), records...
//   record: flags, then varint seconds since the previous record (the first:
//           since the base time), then
//           with POSITION: zigzag varint latitude and longitude change (1e-6 deg;
//             the first: from 0), speed (km/h), course / 2, satellites, HDOP x 10
//             (each a byte), and with ALTITUDE a zigzag varint altitude change (m)
//           with BATTERY: battery voltage (0.1 V, a byte)
//   flags:  bits 0-1 the kind (fix, trip start, trip stop, heartbeat),
//           0x04 POSITION, 0x08 ALTITUDE, 0x10 BATTERY; the rest 0
//
// On the wire the batch is base64 text: Home Assistant Cloud relays a webhook's
// body as a UTF-8 string, which binary doesn't survive (a third more bytes,
// small beside the HTTPS overhead of each post).
//
// The sequence number goes up with each post, from a random start at each
// boot. Records arrive in time order, so the integration drops records no newer
// than the last it took: a batch sent again because its answer was lost. No
// ESPHome dependencies: tests/cpp builds this on the host.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "teltonika.h"

namespace trackcodec {

static constexpr uint8_t COMPACT_VERSION = 1;
static constexpr size_t COMPACT_HEADER_SIZE = 7;
static constexpr uint8_t COMPACT_POSITION = 0x04;
static constexpr uint8_t COMPACT_ALTITUDE = 0x08;
static constexpr uint8_t COMPACT_BATTERY = 0x10;

// Record kinds (Record::kind holds the tracker's Decision; these are the wire values).
enum CompactKind : uint8_t { COMPACT_FIX = 0, COMPACT_START = 1, COMPACT_STOP = 2, COMPACT_HEARTBEAT = 3 };

namespace detail {
inline size_t put_varint(uint8_t *p, uint32_t v) {
  size_t n = 0;
  while (v >= 0x80) {
    p[n++] = static_cast<uint8_t>(v | 0x80);
    v >>= 7;
  }
  p[n++] = static_cast<uint8_t>(v);
  return n;
}
inline uint32_t zigzag(int32_t v) { return (static_cast<uint32_t>(v) << 1) ^ static_cast<uint32_t>(v >> 31); }
inline int32_t micro_degrees(float deg) { return static_cast<int32_t>(std::lround(static_cast<double>(deg) * 1e6)); }
inline uint8_t clamp_byte(float v) {
  if (std::isnan(v))
    return 0;
  return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 255.0f)));
}
}  // namespace detail

/// The base64 length of n bytes.
inline constexpr size_t base64_size(size_t n) { return (n + 2) / 3 * 4; }

/// Base64 (RFC 4648, with padding) of in[0..n) into out, which must hold
/// base64_size(n) characters; returns that size.
inline size_t base64_encode(const uint8_t *in, size_t n, char *out) {
  static const char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0;
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = static_cast<uint32_t>(in[i]) << 16;
    if (i + 1 < n)
      v |= static_cast<uint32_t>(in[i + 1]) << 8;
    if (i + 2 < n)
      v |= in[i + 2];
    out[o++] = ALPHABET[(v >> 18) & 0x3F];
    out[o++] = ALPHABET[(v >> 12) & 0x3F];
    out[o++] = i + 1 < n ? ALPHABET[(v >> 6) & 0x3F] : '=';
    out[o++] = i + 2 < n ? ALPHABET[v & 0x3F] : '=';
  }
  return o;
}

class CompactEncoder {
 public:
  /// Start a batch in out[0..capacity). Returns false when not even the header fits.
  bool begin(uint8_t *out, size_t capacity, uint16_t sequence, uint32_t base_time) {
    if (capacity < COMPACT_HEADER_SIZE)
      return false;
    this->out_ = out;
    this->end_ = out + capacity;
    out[0] = COMPACT_VERSION;
    out[1] = static_cast<uint8_t>(sequence);
    out[2] = static_cast<uint8_t>(sequence >> 8);
    for (int i = 0; i < 4; i++)
      out[3 + i] = static_cast<uint8_t>(base_time >> (8 * i));
    this->p_ = out + COMPACT_HEADER_SIZE;
    this->time_ = base_time;
    this->lat_ = this->lon_ = 0;
    this->alt_ = 0;
    return true;
  }

  /// Add a record of the given kind. Returns false, writing nothing, when it doesn't fit.
  bool add(const Record &r, CompactKind kind) {
    uint8_t tmp[40];
    size_t n = 1;
    uint8_t flags = kind & 0x03;
    // Records are in time order; a clock that stepped back counts as no time.
    n += detail::put_varint(tmp + n, r.time > this->time_ ? r.time - this->time_ : 0);
    int32_t lat = 0, lon = 0;
    if (r.has_location) {
      flags |= COMPACT_POSITION;
      lat = detail::micro_degrees(r.latitude);
      lon = detail::micro_degrees(r.longitude);
      n += detail::put_varint(tmp + n, detail::zigzag(lat - this->lat_));
      n += detail::put_varint(tmp + n, detail::zigzag(lon - this->lon_));
      tmp[n++] = detail::clamp_byte(r.speed_kmh);
      float course = std::isnan(r.course_deg) ? 0.0f : std::fmod(std::fmod(r.course_deg, 360.0f) + 360.0f, 360.0f);
      tmp[n++] = static_cast<uint8_t>(static_cast<int>(course) / 2);
      tmp[n++] = r.satellites;
      tmp[n++] = r.hdop;
      if (r.has_altitude) {
        flags |= COMPACT_ALTITUDE;
        n += detail::put_varint(tmp + n, detail::zigzag(r.altitude_m - this->alt_));
      }
    }
    if (r.battery_mv > 0) {
      flags |= COMPACT_BATTERY;
      tmp[n++] = static_cast<uint8_t>(std::min<uint32_t>(255, (r.battery_mv + 50) / 100));
    }
    tmp[0] = flags;
    if (static_cast<size_t>(this->end_ - this->p_) < n)
      return false;
    std::memcpy(this->p_, tmp, n);
    this->p_ += n;
    if (r.time > this->time_)
      this->time_ = r.time;
    if (r.has_location) {
      this->lat_ = lat;
      this->lon_ = lon;
      if (r.has_altitude)
        this->alt_ = r.altitude_m;
    }
    return true;
  }

  size_t size() const { return static_cast<size_t>(this->p_ - this->out_); }

 protected:
  uint8_t *out_{nullptr};
  uint8_t *end_{nullptr};
  uint8_t *p_{nullptr};
  uint32_t time_{0};
  int32_t lat_{0};
  int32_t lon_{0};
  int32_t alt_{0};
};

}  // namespace trackcodec
