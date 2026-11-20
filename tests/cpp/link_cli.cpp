// Host harness for components/freematics_link/link_parse.h, driven by
// tests/test_freematics_link.py: one command per line, one result per line.
//   gnifo <reply>    -> "none" or date time epoch lat lon alt kmh course sats hdop
//   number <reply>   -> the first number, or "nan"
//   version <reply>  -> the version (14 for 1.4), 0 if unknown
//   end <reply>      -> length before "\r>", or -1
// In <reply>, "\r" and "\n" are written as the two characters backslash-r / backslash-n.
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>

#include "link_parse.h"

using namespace freematics;

static std::string unescape(const std::string &in) {
  std::string out;
  for (size_t i = 0; i < in.size(); i++) {
    if (in[i] == '\\' && i + 1 < in.size() && (in[i + 1] == 'r' || in[i + 1] == 'n')) {
      out += in[i + 1] == 'r' ? '\r' : '\n';
      i++;
    } else {
      out += in[i];
    }
  }
  return out;
}

int main() {
  std::string line;
  while (std::getline(std::cin, line)) {
    size_t sp = line.find(' ');
    std::string cmd = line.substr(0, sp);
    std::string arg = sp == std::string::npos ? "" : unescape(line.substr(sp + 1));
    if (cmd == "gnifo") {
      Fix f{};
      if (parse_gnifo(arg.c_str(), &f)) {
        std::printf("%u %u %u %.6f %.6f %.2f %.2f %.2f %u %u\n", f.date, f.time, f.epoch, f.latitude, f.longitude,
                    f.altitude_m, f.speed_kmh, f.course_deg, f.satellites, f.hdop);
      } else {
        std::printf("none\n");
      }
    } else if (cmd == "number") {
      float v = parse_number(arg.c_str());
      if (std::isnan(v)) {
        std::printf("nan\n");
      } else {
        std::printf("%.2f\n", v);
      }
    } else if (cmd == "version") {
      std::printf("%d\n", parse_version(arg.c_str()));
    } else if (cmd == "end") {
      std::printf("%d\n", reply_end(arg.c_str(), arg.size()));
    }
  }
  return 0;
}
