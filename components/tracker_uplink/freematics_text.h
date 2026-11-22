#pragma once
// Traccar's Freematics protocol (UDP 5170 by default), the text format the
// stock telelogger firmware sends: a fallback beside GH3000, for checking a
// Traccar server with the protocol it already received from these dongles.
// No ESPHome dependencies (tests/cpp checks it against a mirror of Traccar's
// FreematicsProtocolDecoder).
//
//   <id>#0:<n>,11:<ddmmyy>,10:<hhmmsscc>,A:<lat>,B:<lng>,D:<km/h>,E:<course>,F:<sats>,24:<V x 100>*<sum>
//
// Each "0:" starts a position. Traccar doesn't answer positions, only events:
// a login (EV=1) gets "1#EV=1,RX=1,TS=<ts>*<sum>", which shows the server is
// reachable.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "teltonika.h"

namespace trackcodec {

/// Traccar's Checksum.sum: the byte sum, as two hex digits.
inline uint8_t text_checksum(const char *s, size_t n) {
  uint8_t sum = 0;
  for (size_t i = 0; i < n; i++)
    sum += static_cast<uint8_t>(s[i]);
  return sum;
}

namespace detail {
// Append printf output to out at *len; false when it doesn't fit.
template<typename... Args> bool append(char *out, size_t size, size_t *len, const char *fmt, Args... args) {
  if (*len >= size)
    return false;
  int n = snprintf(out + *len, size - *len, fmt, args...);  // NOLINT(cppcoreguidelines-pro-type-vararg)
  if (n < 0 || static_cast<size_t>(n) >= size - *len)
    return false;
  *len += static_cast<size_t>(n);
  return true;
}

inline bool finish(char *out, size_t size, size_t *len) {
  uint8_t sum = text_checksum(out, *len);
  return append(out, size, len, "*%02X", sum);
}
}  // namespace detail

/// The records as one Freematics packet. Returns its length, or 0 when it
/// doesn't fit (send fewer).
inline size_t encode_freematics(char *out, size_t size, const char *device_id, const Record *records, size_t count) {
  size_t len = 0;
  if (!detail::append(out, size, &len, "%s#", device_id))
    return 0;
  for (size_t i = 0; i < count; i++) {
    const Record &r = records[i];
    time_t t = r.time;
    struct tm tm;
    gmtime_r(&t, &tm);
    if (!detail::append(out, size, &len, "%s0:%u,11:%02d%02d%02d,10:%02d%02d%02d00", i == 0 ? "" : ",",
                        static_cast<unsigned>(i), tm.tm_mday, tm.tm_mon + 1, tm.tm_year % 100, tm.tm_hour, tm.tm_min,
                        tm.tm_sec))
      return 0;
    if (r.has_location &&
        !detail::append(out, size, &len, ",A:%.6f,B:%.6f,D:%d,E:%d,F:%u", static_cast<double>(r.latitude),
                        static_cast<double>(r.longitude), static_cast<int>(std::lround(r.speed_kmh)),
                        static_cast<int>(std::lround(r.course_deg) % 360), static_cast<unsigned>(r.satellites)))
      return 0;
    if (r.battery_mv != 0 && !detail::append(out, size, &len, ",24:%u", static_cast<unsigned>(r.battery_mv / 10)))
      return 0;
    // Traccar's Freematics decoder: 81 signal (rssi), 82 device temperature (tenths of a degree)
    if (r.signal_dbm < 0 && !detail::append(out, size, &len, ",81:%d", static_cast<int>(r.signal_dbm)))
      return 0;
    if (r.has_temperature && !detail::append(out, size, &len, ",82:%d", static_cast<int>(r.temperature_c) * 10))
      return 0;
  }
  return detail::finish(out, size, &len) ? len : 0;
}

/// A login event, which Traccar answers.
inline size_t encode_freematics_login(char *out, size_t size, const char *device_id, uint32_t ts) {
  size_t len = 0;
  // Traccar reads it as an event only when it starts with "EV".
  if (!detail::append(out, size, &len, "%s#EV=1,TS=%u,ID=%s", device_id, static_cast<unsigned>(ts), device_id))
    return 0;
  return detail::finish(out, size, &len) ? len : 0;
}

/// Whether data is Traccar's answer to a login: "1#EV=1,RX=1,TS=...".
inline bool is_freematics_login_reply(const uint8_t *data, size_t len) {
  static constexpr char PREFIX[] = "1#EV=1,RX=1";
  return len >= sizeof(PREFIX) - 1 && memcmp(data, PREFIX, sizeof(PREFIX) - 1) == 0;
}

}  // namespace trackcodec
