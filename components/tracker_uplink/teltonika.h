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
  uint8_t hdop;   // HDOP x 10, as the co-processor reports it (compact batches only)
  uint8_t kind;   // why it was recorded: the tracker's Decision (compact batches only)
  // Extras that ride along with trip starts, stops and heartbeats (and impacts):
  bool has_temperature;      // temperature_c is known
  int8_t temperature_c;      // the dongle's (its motion sensor's) temperature (GH3000 IO 72)
  int8_t signal_dbm;         // cellular signal; 0: unknown (GH3000 IO 205, as -dBm)
  uint8_t impact_dg;         // an impact's peak in tenths of g; 0: none (GH3000 IO 247)
  uint16_t next_report_min;  // the next report comes within this many minutes (going to sleep); 0: not said
};

// GH3000 IO elements Traccar keeps as io<id> attributes (its GH3000 decoder
// names only a few: 67 battery, 240 motion). Values are unsigned there.
static constexpr uint8_t IO_TEMPERATURE = 72;  // degrees C; above 127 is negative (subtract 256)
static constexpr uint8_t IO_SIGNAL = 205;      // cellular signal, -dBm (85: -85 dBm)
static constexpr uint8_t IO_IMPACT = 247;      // an impact's peak, tenths of g
static constexpr uint8_t IO_BATTERY = 67;      // millivolts (Traccar: battery)
static constexpr uint8_t IO_MOTION = 240;      // 0 or 1 (Traccar: motion)

// The 1-byte IO elements a record carries, in ids[] and values[]; returns how many.
inline size_t io_bytes(const Record &r, uint8_t *ids, uint8_t *values) {
  size_t n = 0;
  if (r.motion >= 0) {
    ids[n] = IO_MOTION;
    values[n++] = r.motion ? 1 : 0;
  }
  if (r.has_temperature) {
    ids[n] = IO_TEMPERATURE;
    values[n++] = static_cast<uint8_t>(r.temperature_c);
  }
  if (r.signal_dbm < 0) {
    ids[n] = IO_SIGNAL;
    values[n++] = static_cast<uint8_t>(-r.signal_dbm);
  }
  if (r.impact_dg > 0) {
    ids[n] = IO_IMPACT;
    values[n++] = r.impact_dg;
  }
  return n;
}

// Bytes a record takes in a packet.
inline size_t record_size(const Record &r) {
  size_t n = 4 + 1;  // time, global mask
  if (r.has_location)
    n += 1 + 8 + 1 + 1 + 1 + (r.has_altitude ? 2 : 0);  // location mask, lat/lon, altitude, course, speed, sats
  uint8_t ids[4], values[4];
  size_t bytes = io_bytes(r, ids, values);
  if (bytes > 0)
    n += 1 + 2 * bytes;  // count, then an id and a 1-byte value each
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
    uint8_t ids[4], values[4];
    size_t bytes = io_bytes(r, ids, values);
    uint8_t global = r.has_location ? 0x01 : 0x00;
    if (bytes > 0)
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
    if (bytes > 0) {
      put8(p, static_cast<uint8_t>(bytes));
      for (size_t j = 0; j < bytes; j++) {
        put8(p, ids[j]);
        put8(p, values[j]);
      }
    }
    if (r.battery_mv > 0) {
      put8(p, 1);
      put8(p, IO_BATTERY);
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

}  // namespace trackcodec
