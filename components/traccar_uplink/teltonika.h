#pragma once
// Teltonika "GH3000" (codec 7) over UDP, as Traccar's Teltonika decoder reads it
// (traccar/src/main/java/org/traccar/protocol/TeltonikaProtocolDecoder.java:
// decodeUdp, parseData, decodeLocation, decodeGh3000Parameter).
//
// Chosen because it is the densest location record Traccar accepts: 17 bytes
// for time, position, course, speed and satellites, with optional IO values,
// and Traccar acknowledges each UDP packet, so the device knows what arrived.
//
// No ESPHome dependencies: tests/cpp builds this on the host.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstddef>

namespace trackcodec {

// Seconds between 1970-01-01 and 2007-01-01, the GH3000 epoch.
static constexpr uint32_t GH3000_EPOCH = 1167609600UL;

struct Record {
  uint32_t time;  // Unix seconds, UTC
  float latitude;
  float longitude;
  float speed_kmh;
  float course_deg;
  uint16_t battery_mv;  // 0: not sent (GH3000 IO 67)
  int16_t altitude_m;
  bool has_location;  // false: a time-only record; Traccar keeps the last position
  bool has_altitude;  // with a location: send altitude_m (2 bytes)
  uint8_t satellites;
  int8_t motion;  // -1: not sent; 0 or 1 (GH3000 IO 240)
};

// Bytes a record takes in a packet.
inline size_t record_size(const Record &r) {
  size_t n = 4 + 1;  // time, global mask
  if (r.has_location)
    n += 1 + 8 + 1 + 1 + 1 + (r.has_altitude ? 2 : 0);  // location mask, lat/lon, altitude, course, speed, sats
  if (r.motion >= 0)
    n += 1 + 1 + 1;  // count, id, 1-byte value
  if (r.battery_mv > 0)
    n += 1 + 1 + 2;  // count, id, 2-byte value
  return n;
}

inline size_t header_size(size_t id_len) { return 2 + 2 + 1 + 1 + 2 + id_len + 1 + 1; }

// Size of a packet carrying records[0..count): header, records, trailing count.
inline size_t packet_size(size_t id_len, const Record *records, size_t count) {
  size_t n = header_size(id_len) + 1;
  for (size_t i = 0; i < count; i++)
    n += record_size(records[i]);
  return n;
}

namespace detail {
inline void put8(uint8_t *&p, uint8_t v) { *p++ = v; }
inline void put16(uint8_t *&p, uint16_t v) {
  *p++ = v >> 8;
  *p++ = v & 0xff;
}
inline void put32(uint8_t *&p, uint32_t v) {
  *p++ = v >> 24;
  *p++ = (v >> 16) & 0xff;
  *p++ = (v >> 8) & 0xff;
  *p++ = v & 0xff;
}
inline void put_float(uint8_t *&p, float f) {
  uint32_t v;
  std::memcpy(&v, &f, sizeof(v));
  put32(p, v);
}
}  // namespace detail

// Encode one UDP packet into out (size cap bytes). Returns its length, or 0 if
// it doesn't fit or count is 0 or over 255.
inline size_t encode_packet(uint8_t *out, size_t cap, uint16_t packet_id, uint8_t location_packet_id,
                            const char *device_id, const Record *records, size_t count) {
  using namespace detail;
  size_t id_len = std::strlen(device_id);
  if (count == 0 || count > 255 || id_len == 0 || id_len > 64)
    return 0;
  size_t total = packet_size(id_len, records, count);
  if (total > cap)
    return 0;
  uint8_t *p = out;
  put16(p, total - 2);  // length of what follows
  put16(p, packet_id);
  put8(p, 0x01);  // packet type: data, acknowledgement wanted
  put8(p, location_packet_id);
  put16(p, id_len);
  std::memcpy(p, device_id, id_len);  // NOLINT(bugprone-not-null-terminated-result): length-prefixed
  p += id_len;
  put8(p, 0x07);  // codec: GH3000
  put8(p, count);
  for (size_t i = 0; i < count; i++) {
    const Record &r = records[i];
    uint32_t t = r.time > GH3000_EPOCH ? r.time - GH3000_EPOCH : 0;
    put32(p, t & 0x3fffffff);
    uint8_t global = r.has_location ? 0x01 : 0x00;
    if (r.motion >= 0)
      global |= 0x02;
    if (r.battery_mv > 0)
      global |= 0x04;
    put8(p, global);
    if (r.has_location) {
      put8(p, r.has_altitude ? 0x1F : 0x1D);  // latitude/longitude, (altitude,) course, speed, satellites
      put_float(p, r.latitude);
      put_float(p, r.longitude);
      if (r.has_altitude)  // Traccar reads it unsigned: below sea level reads as 0
        put16(p, static_cast<uint16_t>(r.altitude_m < 0 ? 0 : r.altitude_m));
      float course = r.course_deg;
      while (course < 0)
        course += 360;
      while (course >= 360)
        course -= 360;
      put8(p, static_cast<uint8_t>(course * 256.0f / 360.0f));
      float speed = r.speed_kmh < 0 ? 0 : (r.speed_kmh > 255 ? 255 : r.speed_kmh);
      put8(p, static_cast<uint8_t>(std::lround(speed)));
      put8(p, r.satellites);
    }
    if (r.motion >= 0) {
      put8(p, 1);
      put8(p, 240);
      put8(p, r.motion ? 1 : 0);
    }
    if (r.battery_mv > 0) {
      put8(p, 1);
      put8(p, 67);
      put16(p, r.battery_mv);
    }
  }
  put8(p, count);
  return p - out;
}

// Traccar's answer to a packet: 00 05 00 00 01 <location packet id> <records accepted>.
// Returns the number of records accepted, or -1 if this isn't an answer to location_packet_id.
inline int parse_ack(const uint8_t *buf, size_t len, uint8_t location_packet_id) {
  if (len < 7)
    return -1;
  if (buf[0] != 0x00 || buf[1] != 0x05 || buf[4] != 0x01)
    return -1;
  if (buf[5] != location_packet_id)
    return -1;
  return buf[6];
}

// GNSS date (ddmmyy) and time (hhmmsscc), as the Freematics co-processor reports
// them, to Unix seconds. Returns 0 for an invalid date.
inline uint32_t gnss_to_unix(uint32_t ddmmyy, uint32_t hhmmsscc) {
  uint32_t day = ddmmyy / 10000, month = (ddmmyy / 100) % 100, year = 2000 + ddmmyy % 100;
  uint32_t hour = hhmmsscc / 1000000, minute = (hhmmsscc / 10000) % 100, second = (hhmmsscc / 100) % 100;
  if (day < 1 || day > 31 || month < 1 || month > 12 || year < 2020 || hour > 23 || minute > 59 || second > 60)
    return 0;
  // days from civil (Howard Hinnant's algorithm)
  int y = static_cast<int>(year) - (month <= 2 ? 1 : 0);
  int era = y / 400;
  unsigned yoe = static_cast<unsigned>(y - era * 400);
  unsigned m = month;
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  int64_t days = static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
  return static_cast<uint32_t>(days * 86400 + hour * 3600 + minute * 60 + second);
}

}  // namespace trackcodec
