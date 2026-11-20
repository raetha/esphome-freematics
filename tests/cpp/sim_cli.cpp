// Simulates a day through the tracking filter and the GH3000 encoder, the way
// traccar_uplink batches records, and prints what goes over the network.
//
//   sim_cli <drive minutes> <parked hours on LTE> [send interval s] [heartbeat s]
//
// The drive is generated (fixed seed): fixes every 2 s, town and highway
// speeds, turns, traffic-light stops shorter than the stop delay. While parked
// on LTE only heartbeats go out, each sent at once. Bytes count the UDP payload
// plus 28 bytes of IP and UDP headers, both ways (Traccar's 7-byte answer).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "teltonika.h"
#include "tracker.h"

using namespace trackcodec;

static constexpr size_t HEADERS = 28;  // IPv4 + UDP
static constexpr size_t ACK = 7;
static constexpr size_t BATCH_MAX = 40;
static uint32_t SEND_INTERVAL = 60;

struct Totals {
  size_t records = 0, packets = 0, bytes = 0;
};

static uint32_t rng = 12345;
static float rnd() {
  rng = rng * 1103515245u + 12345u;
  return static_cast<float>((rng >> 8) & 0xFFFF) / 65535.0f;
}

static void send(std::vector<Record> &queue, Totals &t) {
  uint8_t buf[1024];
  while (!queue.empty()) {
    size_t n = queue.size() < BATCH_MAX ? queue.size() : BATCH_MAX;
    size_t len = encode_packet(buf, sizeof(buf), 1, 1, "car1", queue.data(), n);
    while (len == 0 && n > 1)
      len = encode_packet(buf, sizeof(buf), 1, 1, "car1", queue.data(), --n);
    t.packets++;
    t.bytes += len + HEADERS + ACK + HEADERS;
    queue.erase(queue.begin(), queue.begin() + n);
  }
}

int main(int argc, char **argv) {
  if (argc < 3)
    return 2;
  if (argc > 3)
    SEND_INTERVAL = static_cast<uint32_t>(atoi(argv[3]));
  TrackerConfig config;
  if (argc > 4)
    config.heartbeat_s = static_cast<uint32_t>(atoi(argv[4]));
  uint32_t drive_s = static_cast<uint32_t>(atof(argv[1]) * 60);
  uint32_t parked_s = static_cast<uint32_t>(atof(argv[2]) * 3600);

  Tracker tracker(config);  // the packages' defaults unless given
  std::vector<Record> queue;
  Totals drive, parked;
  uint32_t now = 1800000000, last_send = now;
  float speed = 0, course = 90, lat = 43.8, lon = -72.3;
  uint32_t stop_left = 0, cruise_left = 0;
  bool highway = false;

  auto record = [&](Decision d, Totals &t) {
    if (d == Decision::NONE)
      return;
    Record r{};
    r.time = now;
    r.has_location = true;
    r.latitude = lat;
    r.longitude = lon;
    r.speed_kmh = speed;
    r.course_deg = course;
    r.satellites = 10;
    r.motion = -1;
    if (d == Decision::START || d == Decision::STOP || d == Decision::HEARTBEAT) {
      r.motion = tracker.moving() ? 1 : 0;
      r.battery_mv = 12600;
    }
    queue.push_back(r);
    t.records++;
    if (d == Decision::STOP || d == Decision::HEARTBEAT || queue.size() >= BATCH_MAX) {
      send(queue, t);
      last_send = now;
    }
  };

  // The drive, then the stop delay running out.
  for (uint32_t s = 0; s < drive_s + 200; s += 2, now += 2) {
    if (s >= drive_s) {
      speed = 0;
    } else if (stop_left > 0) {
      stop_left = stop_left > 2 ? stop_left - 2 : 0;
      speed = std::fmax(0.0f, speed - 15);
    } else {
      if (cruise_left == 0) {
        cruise_left = 30 + static_cast<uint32_t>(rnd() * 240);
        highway = rnd() < 0.4f;
        if (rnd() < 0.5f)
          course = std::fmod(course + (rnd() < 0.5f ? 90 : 270), 360);  // a turn at a junction
        if (!highway && rnd() < 0.5f)
          stop_left = 20 + static_cast<uint32_t>(rnd() * 60);  // a light or a junction
      }
      cruise_left = cruise_left > 2 ? cruise_left - 2 : 0;
      float target = highway ? 100 : 45;
      speed += (target - speed) * 0.2f + (rnd() - 0.5f) * 4;
      course = std::fmod(course + (rnd() - 0.5f) * (highway ? 2 : 8) + 360, 360);
    }
    lat += speed / 3600.0f * 2 / 111.0f * std::cos(course * 3.14159f / 180);
    lon += speed / 3600.0f * 2 / 80.0f * std::sin(course * 3.14159f / 180);
    record(tracker.on_fix(now, speed, course), drive);
    if (now - last_send >= SEND_INTERVAL && !queue.empty()) {
      send(queue, drive);
      last_send = now;
    }
  }
  send(queue, drive);

  // Parked on LTE: a fix every 2 s, standing still.
  for (uint32_t s = 0; s < parked_s; s += 2, now += 2)
    record(tracker.on_fix(now, 0, course), parked);
  send(queue, parked);

  printf("drive %zu records %zu packets %zu bytes\n", drive.records, drive.packets, drive.bytes);
  printf("parked %zu records %zu packets %zu bytes\n", parked.records, parked.packets, parked.bytes);
  return 0;
}
