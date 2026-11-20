// Drives components/file_logger/log_line.h for tests/test_file_logger.py.
//
//   log_line_cli clean <text with \e for ESC>      -> the cleaned line
//   log_line_cli ring <capacity> <op>...           ops: push:<text> | take:<n>
//     prints after each op: ok/full, then the ring's contents in order
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "log_line.h"

static std::string unescape(const char *s) {
  std::string out;
  for (; *s != '\0'; s++) {
    if (s[0] == '\\' && s[1] == 'e') {
      out += '\033';
      s++;
    } else {
      out += *s;
    }
  }
  return out;
}

static std::string contents(const filelog::ByteRing &ring) {
  filelog::ByteRing copy;  // read through a copy so the original keeps its bytes
  std::string out;
  copy.init(ring.capacity());
  const char *data;
  size_t n = ring.peek(&data);
  out.append(data, n);
  return out;
}

int main(int argc, char **argv) {
  if (argc >= 3 && strcmp(argv[1], "clean") == 0) {
    std::string in = unescape(argv[2]);
    char out[64];
    size_t n = filelog::clean_line(in.data(), in.size(), out, sizeof(out));
    printf("%zu|%s\n", n, out);
    return 0;
  }
  if (argc >= 3 && strcmp(argv[1], "ring") == 0) {
    filelog::ByteRing ring;
    ring.init(atoi(argv[2]));
    for (int i = 3; i < argc; i++) {
      if (strncmp(argv[i], "push:", 5) == 0) {
        const char *t = argv[i] + 5;
        printf("%s", ring.push(t, strlen(t)) ? "ok" : "full");
      } else if (strncmp(argv[i], "take:", 5) == 0) {
        const char *data;
        size_t n = ring.peek(&data);
        size_t want = atoi(argv[i] + 5);
        if (n > want)
          n = want;
        printf("took %.*s", (int) n, data);
        ring.consume(n);
      }
      printf(" [%zu] %s\n", ring.size(), contents(ring).c_str());
    }
    return 0;
  }
  return 2;
}
