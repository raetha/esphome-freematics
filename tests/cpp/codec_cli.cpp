// Host harness for components/traccar_uplink: encodes packets and runs the
// tracker from commands on stdin, printing results for tests/test_traccar_uplink.py.
//
//   packet <packet_id> <location_packet_id> <device_id> <n> then n lines:
//     <time> <has_location> <lat> <lon> <speed> <course> <sats> <battery_mv> <motion>
//   -> hex of the packet
//   compact <capacity> <sequence> <base_time> <n> then n lines:
//     <kind> <time> <has_location> <lat> <lon> <speed> <course> <sats> <hdop> <battery_mv> [<altitude> if has == 2]
//   -> records added, then the hex of the batch
//   ack <hex> <location_packet_id>  -> accepted count or -1
//   unix <ddmmyy> <hhmmsscc>        -> Unix seconds
//   track <moving_speed> <stop_delay> <moving_interval> <turn_angle> <min_turn> <heartbeat> <n>
//     then n lines: fix <time> <speed> <course> | at <time> <speed> <course> <lat> <lon> | tick <time>
//       | car <time> <0|1> (whether a trip may start; prints NONE)
//   -> one decision per line: NONE RECORD START STOP HEARTBEAT
//   queue <capacity> <n> then n ops: push <time> | pop <count> | erase <index>
//   -> after each op: the record times in order, or "-" when empty, and "full" when full
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "compact.h"
#include "record_queue.h"
#include "freematics_text.h"
#include "teltonika.h"
#include "tracker.h"

using namespace trackcodec;

int main() {
  char cmd[32];
  while (std::scanf("%31s", cmd) == 1) {
    if (!std::strcmp(cmd, "packet")) {
      unsigned pid, lpid, n;
      char id[80];
      std::scanf("%u %u %79s %u", &pid, &lpid, id, &n);
      std::vector<Record> rs(n);
      for (auto &r : rs) {
        unsigned t, has, sats, mv;
        int motion;
        std::scanf("%u %u %f %f %f %f %u %u %d", &t, &has, &r.latitude, &r.longitude, &r.speed_kmh, &r.course_deg,
                   &sats, &mv, &motion);
        r.time = t;
        r.has_location = has != 0;
        r.satellites = sats;
        r.battery_mv = mv;
        r.motion = motion;
        if (has == 2) {  // a location with altitude: one more field, metres
          int alt;
          std::scanf("%d", &alt);
          r.has_altitude = true;
          r.altitude_m = static_cast<int16_t>(alt);
        }
      }
      uint8_t buf[2048];
      size_t len = encode_packet(buf, sizeof(buf), pid, lpid, id, rs.data(), rs.size());
      for (size_t i = 0; i < len; i++)
        std::printf("%02x", buf[i]);
      std::printf("\n");
    } else if (!std::strcmp(cmd, "compact")) {
      unsigned cap, seq, base, n;
      std::scanf("%u %u %u %u", &cap, &seq, &base, &n);
      uint8_t buf[2048];
      CompactEncoder enc;
      enc.begin(buf, cap < sizeof(buf) ? cap : sizeof(buf), static_cast<uint16_t>(seq), base);
      unsigned added = 0;
      bool full = false;
      for (unsigned i = 0; i < n; i++) {
        Record r{};
        unsigned kind, t, has, sats, hdop, mv;
        std::scanf("%u %u %u %f %f %f %f %u %u %u", &kind, &t, &has, &r.latitude, &r.longitude, &r.speed_kmh,
                   &r.course_deg, &sats, &hdop, &mv);
        r.time = t;
        r.has_location = has != 0;
        r.satellites = sats;
        r.hdop = hdop;
        r.battery_mv = mv;
        if (has == 2) {
          int alt;
          std::scanf("%d", &alt);
          r.has_altitude = true;
          r.altitude_m = static_cast<int16_t>(alt);
        }
        if (!full && enc.add(r, static_cast<CompactKind>(kind)))
          added++;
        else
          full = true;
      }
      std::printf("%u\n", added);
      for (size_t i = 0; i < enc.size(); i++)
        std::printf("%02x", buf[i]);
      std::printf("\n");
    } else if (!std::strcmp(cmd, "freematics")) {
      // freematics <id> <size> <n>, then n records as for packet: the text packet, or "-"
      unsigned size, n;
      char id[80];
      std::scanf("%79s %u %u", id, &size, &n);
      std::vector<Record> rs(n);
      for (auto &r : rs) {
        unsigned t, has, sats, mv;
        int motion;
        std::scanf("%u %u %f %f %f %f %u %u %d", &t, &has, &r.latitude, &r.longitude, &r.speed_kmh, &r.course_deg,
                   &sats, &mv, &motion);
        r.time = t;
        r.has_location = has;
        r.satellites = sats;
        r.battery_mv = mv;
        r.motion = motion;
      }
      std::vector<char> buf(size);
      size_t len = encode_freematics(buf.data(), buf.size(), id, rs.data(), rs.size());
      std::printf("%.*s\n", len ? static_cast<int>(len) : 1, len ? buf.data() : "-");
    } else if (!std::strcmp(cmd, "login")) {
      unsigned ts;
      char id[80];
      std::scanf("%79s %u", id, &ts);
      char buf[128];
      size_t len = encode_freematics_login(buf, sizeof(buf), id, ts);
      std::printf("%.*s\n", static_cast<int>(len), buf);
    } else if (!std::strcmp(cmd, "loginreply")) {
      char text[80];
      std::scanf("%79s", text);
      std::printf("%d\n", is_freematics_login_reply(reinterpret_cast<const uint8_t *>(text), std::strlen(text)));
    } else if (!std::strcmp(cmd, "ack")) {
      char hex[64];
      unsigned lpid;
      std::scanf("%63s %u", hex, &lpid);
      uint8_t b[32];
      size_t n = std::strlen(hex) / 2;
      for (size_t i = 0; i < n; i++)
        std::sscanf(hex + 2 * i, "%2hhx", &b[i]);
      std::printf("%d\n", parse_ack(b, n, lpid));
    } else if (!std::strcmp(cmd, "unix")) {
      unsigned d, t;
      std::scanf("%u %u", &d, &t);
      std::printf("%u\n", gnss_to_unix(d, t));
    } else if (!std::strcmp(cmd, "track")) {
      TrackerConfig c;
      unsigned stop, interval, min_turn, hb, start_fixes, n;
      std::scanf("%f %u %u %f %u %u %u %f %u", &c.moving_speed_kmh, &stop, &interval, &c.turn_angle_deg, &min_turn, &hb,
                 &start_fixes, &c.start_distance_m, &n);
      c.start_fixes = static_cast<uint8_t>(start_fixes);
      c.stop_delay_s = stop;
      c.moving_interval_s = interval;
      c.min_turn_interval_s = min_turn;
      c.heartbeat_s = hb;
      Tracker tr(c);
      static const char *names[] = {"NONE", "RECORD", "START", "STOP", "HEARTBEAT"};
      for (unsigned i = 0; i < n; i++) {
        char kind[8];
        unsigned t;
        std::scanf("%7s %u", kind, &t);
        Decision d;
        if (!std::strcmp(kind, "fix")) {
          float speed, course;
          std::scanf("%f %f", &speed, &course);
          d = tr.on_fix(t, speed, course);
        } else if (!std::strcmp(kind, "at")) {  // a fix with a position
          float speed, course, lat, lon;
          std::scanf("%f %f %f %f", &speed, &course, &lat, &lon);
          d = tr.on_fix(t, speed, course, lat, lon);
        } else if (!std::strcmp(kind, "car")) {
          unsigned on;
          std::scanf("%u", &on);
          tr.set_can_start(on != 0);
          d = Decision::NONE;
        } else {
          d = tr.on_tick(t);
        }
        std::printf("%s\n", names[static_cast<int>(d)]);
      }
    } else if (!std::strcmp(cmd, "queue")) {
      unsigned cap, n;
      std::scanf("%u %u", &cap, &n);
      RecordQueue q;
      q.init(cap);
      for (unsigned i = 0; i < n; i++) {
        char op[8];
        unsigned arg;
        std::scanf("%7s %u", op, &arg);
        if (!std::strcmp(op, "push")) {
          Record r{};
          r.time = arg;
          if (!q.push_back(r))
            std::printf("rejected ");
        } else if (!std::strcmp(op, "pop")) {
          q.pop_front(arg);
        } else {
          q.erase(arg);
        }
        if (q.empty())
          std::printf("-");
        for (size_t j = 0; j < q.size(); j++)
          std::printf("%s%u", j ? "," : "", q.at(j).time);
        std::printf("%s\n", q.full() ? " full" : "");
      }
    }
  }
  return 0;
}
