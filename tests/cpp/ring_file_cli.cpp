// Drives components/file_queue/ring_file.h for tests/test_file_queue.py.
//
//   ring_file_cli <path> <record size> <format> <capacity> <op>...
//
// Records are record-size bytes, each filled with one byte value. Ops:
//   push:<v>      add a record of byte v; prints "push ok" or "push no"
//   pop:<n>       remove n records; prints "pop ok"
//   read:<i>:<n>  print the byte values of n records from the i-th oldest, or "read no"
//   size          print the record count
//   sync          sync to the disk; prints "sync ok", "sync no", and whether it was dirty
// The first line printed is how the file was opened.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "ring_file.h"

int main(int argc, char **argv) {
  if (argc < 5)
    return 2;
  auto record_size = static_cast<uint16_t>(atoi(argv[2]));
  auto format = static_cast<uint16_t>(atoi(argv[3]));
  auto capacity = static_cast<uint32_t>(atoi(argv[4]));
  ringfile::RingFile q;
  static const char *const NAMES[] = {"loaded", "created", "replaced", "failed"};
  printf("%s\n", NAMES[static_cast<int>(q.open(argv[1], record_size, format, capacity))]);
  std::vector<uint8_t> rec(record_size);
  for (int i = 5; i < argc; i++) {
    const char *op = argv[i];
    if (strncmp(op, "push:", 5) == 0) {
      memset(rec.data(), atoi(op + 5), rec.size());
      printf("push %s\n", q.push(rec.data()) ? "ok" : "no");
    } else if (strncmp(op, "pop:", 4) == 0) {
      printf("pop %s\n", q.pop(atoi(op + 4)) ? "ok" : "no");
    } else if (strncmp(op, "read:", 5) == 0) {
      unsigned first = 0, n = 0;
      if (sscanf(op + 5, "%u:%u", &first, &n) != 2)
        return 2;
      std::vector<uint8_t> out(size_t(n) * record_size);
      if (!q.read(first, n, out.data())) {
        printf("read no\n");
        continue;
      }
      printf("read");
      for (unsigned r = 0; r < n; r++) {
        bool same = true;
        for (unsigned b = 0; b < record_size; b++)
          same = same && out[size_t(r) * record_size + b] == out[size_t(r) * record_size];
        printf(same ? " %u" : " ?", out[size_t(r) * record_size]);
      }
      printf("\n");
    } else if (strcmp(op, "sync") == 0) {
      bool was_dirty = q.dirty();
      printf("sync %s%s\n", q.sync() ? "ok" : "no", was_dirty ? " (dirty)" : "");
    } else if (strcmp(op, "size") == 0) {
      printf("size %u\n", static_cast<unsigned>(q.size()));
    } else {
      return 2;
    }
  }
  return 0;
}
