// Pulls the component's plain C++ headers into one translation unit so clang-tidy can
// check them on the host with ESPHome's rules (.clang-tidy). The ESPHome-facing files
// are checked by the firmware compile.
#include "compact.h"
#include "record_queue.h"
#include "teltonika.h"
#include "tracker.h"
#include "trip.h"
#include "motion.h"
#include "link_parse.h"
#include "backlog.h"
#include "ring_file.h"
#include "pending_ops.h"
#include "log_line.h"
#include "freematics_text.h"
