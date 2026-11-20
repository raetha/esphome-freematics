// Drives components/traccar_uplink/backlog.h with a ring file as its store, for
// tests/test_traccar_uplink.py.
//
//   backlog_cli <memory capacity> <store path or -> <store capacity> <op>...
//
// Records are told apart by their time. Ops:
//   add:<time>:<in flight>  prints "added", "dropped oldest" or "dropped"
//   ack:<n>                 Traccar acknowledged the n oldest
//   fail                    the store stops working (a card pulled out)
//   restart                 a new Backlog over a reopened store
//   show                    prints "size <n> memory <times...>"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "backlog.h"
#include "ring_file.h"

using trackcodec::Added;
using trackcodec::Backlog;
using trackcodec::Record;

struct HostStore {
  ringfile::RingFile file;
  bool is_ready() const { return this->file.is_open(); }
  uint32_t size() const { return this->file.size(); }
  bool full() const { return this->file.full(); }
  bool push(const void *r) { return this->file.push(r); }
  bool read(uint32_t i, uint32_t n, void *out) { return this->file.read(i, n, out); }
  bool pop(uint32_t n) { return this->file.pop(n); }
};

int main(int argc, char **argv) {
  if (argc < 4)
    return 2;
  size_t memory = atoi(argv[1]);
  const char *path = argv[2];
  uint32_t store_capacity = atoi(argv[3]);
  bool use_store = strcmp(path, "-") != 0;

  HostStore store;
  auto backlog = std::make_unique<Backlog<HostStore>>();
  auto start = [&]() {
    if (use_store)
      store.file.open(path, sizeof(Record), 1, store_capacity);
    backlog = std::make_unique<Backlog<HostStore>>();
    backlog->init(memory, use_store ? &store : nullptr);
  };
  start();

  for (int i = 4; i < argc; i++) {
    const char *op = argv[i];
    if (strncmp(op, "add:", 4) == 0) {
      unsigned time = 0, in_flight = 0;
      if (sscanf(op + 4, "%u:%u", &time, &in_flight) != 2)
        return 2;
      Record r{};
      r.time = time;
      static const char *const NAMES[] = {"added", "dropped oldest", "dropped"};
      printf("%s\n", NAMES[static_cast<int>(backlog->add(r, in_flight))]);
    } else if (strncmp(op, "ack:", 4) == 0) {
      backlog->acknowledged(atoi(op + 4));
    } else if (strcmp(op, "fail") == 0) {
      store.file.close();
    } else if (strcmp(op, "restart") == 0) {
      start();
    } else if (strcmp(op, "show") == 0) {
      printf("size %u memory", static_cast<unsigned>(backlog->size()));
      for (size_t j = 0; j < backlog->memory().size(); j++)
        printf(" %u", static_cast<unsigned>(backlog->memory().at(j).time));
      printf("\n");
    } else {
      return 2;
    }
  }
  return 0;
}
