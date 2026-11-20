#pragma once
// Parsing the Freematics co-processor's replies, as the FreematicsPlus library
// does (FreematicsPlus.cpp: gpsGetData, getDeviceType; FreematicsOBD.cpp:
// getResultValue, getVoltage). No ESPHome dependencies: tests/cpp builds this
// on the host.

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace freematics {

struct Fix {
  uint32_t date;   // ddmmyy, UTC
  uint32_t time;   // hhmmsscc, UTC
  uint32_t epoch;  // Unix seconds; 0 when date/time aren't valid
  float latitude;
  float longitude;
  float altitude_m;
  float speed_kmh;
  float course_deg;
  uint8_t satellites;
  uint8_t hdop;  // as the co-processor reports it
};

// GNSS date (ddmmyy) and time (hhmmsscc) to Unix seconds; 0 for an invalid date.
inline uint32_t gnss_to_unix(uint32_t ddmmyy, uint32_t hhmmsscc) {
  uint32_t day = ddmmyy / 10000, month = (ddmmyy / 100) % 100, year = 2000 + ddmmyy % 100;
  uint32_t hour = hhmmsscc / 1000000, minute = (hhmmsscc / 10000) % 100, second = (hhmmsscc / 100) % 100;
  if (day < 1 || day > 31 || month < 1 || month > 12 || year < 2020 || hour > 23 || minute > 59 || second > 60)
    return 0;
  int y = static_cast<int>(year) - (month <= 2 ? 1 : 0);
  int era = y / 400;
  unsigned yoe = static_cast<unsigned>(y - era * 400);
  unsigned m = month;
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  int64_t days = static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
  return static_cast<uint32_t>(days * 86400 + hour * 3600 + minute * 60 + second);
}

// Reply to ATGPS: "...$GNIFO,date,time,lat*1e6,lng*1e6,alt*100,knots*100,heading*100,sats,hdop".
// Returns true and fills fix for a usable position (not 0,0).
inline bool parse_gnifo(const char *reply, Fix *fix) {
  const char *s = std::strstr(reply, "$GNIFO,");
  if (s == nullptr)
    return false;
  s += 7;
  long v[9] = {0};  // NOLINT(google-runtime-int): atol's type
  int n = 0;
  while (n < 9 && *s != '\0') {
    v[n++] = std::strtol(s, nullptr, 10);
    s = std::strchr(s, ',');
    if (s == nullptr)
      break;
    s++;
  }
  if (n < 4 || (v[2] == 0 && v[3] == 0))
    return false;
  if (v[2] < -90000000 || v[2] > 90000000 || v[3] < -180000000 || v[3] > 180000000)
    return false;
  fix->date = static_cast<uint32_t>(v[0]);
  fix->time = static_cast<uint32_t>(v[1]);
  fix->epoch = gnss_to_unix(fix->date, fix->time);
  fix->latitude = static_cast<float>(v[2]) / 1e6f;
  fix->longitude = static_cast<float>(v[3]) / 1e6f;
  fix->altitude_m = n > 4 ? static_cast<float>(v[4]) / 100.0f : NAN;
  fix->speed_kmh = n > 5 ? static_cast<float>(v[5]) / 100.0f * 1.852f : NAN;
  fix->course_deg = n > 6 ? static_cast<float>(v[6]) / 100.0f : NAN;
  fix->satellites = n > 7 ? static_cast<uint8_t>(v[7]) : 0;
  fix->hdop = n > 8 ? static_cast<uint8_t>(v[8]) : 0;
  return true;
}

// The library drops a fix that jumps more than 0.1 degrees from the previous one.
inline bool plausible_jump(const Fix &previous, const Fix &next) {
  return std::fabs(previous.latitude - next.latitude) < 0.1f && std::fabs(previous.longitude - next.longitude) < 0.1f;
}

// The first number in a reply (getResultValue): "ATRV\r12.6V" -> 12.6. NAN if none.
inline float parse_number(const char *reply) {
  const char *p = reply;
  while (p != nullptr && *p != '\0') {
    if ((*p >= '0' && *p <= '9') || *p == '-')
      return std::strtof(p, nullptr);
    p = std::strchr(p, '\r');
    if (p == nullptr)
      break;
    p++;
    if (*p == '\n')
      p++;
  }
  return NAN;
}

// The co-processor's version from its ATI reply (assumed "... OBD v1.4"; the library
// reads "OBD", then the character two past the next space, as "d.d"): 1.4 -> 14.
// 0 when it can't tell.
inline int parse_version(const char *reply) {
  const char *p = std::strstr(reply, "OBD");
  if (p != nullptr)
    p = std::strchr(p, ' ');
  if (p == nullptr)
    return 0;
  p += 2;
  if (p[0] >= '0' && p[0] <= '9' && p[1] == '.' && p[2] >= '0' && p[2] <= '9')
    return (p[0] - '0') * 10 + (p[2] - '0');
  return 0;
}

// Replies end with "\r>" (CLink_UART::receive). Returns the length of the reply
// before that marker, or -1 when the reply isn't complete yet.
inline int reply_end(const char *buf, size_t len) {
  for (size_t i = 1; i < len; i++) {
    if (buf[i - 1] == '\r' && buf[i] == '>')
      return static_cast<int>(i - 1);
  }
  return -1;
}

}  // namespace freematics
