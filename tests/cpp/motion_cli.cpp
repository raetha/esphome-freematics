// Host harness for components/icm426xx/motion.h: reads
//   <rate_hz> <motion_g> <impact_g> then samples "x y z" (g), one per line,
// and prints one line per sample: "moved" and/or "impact <peak>", or "-".
#include <cstdio>

#include "motion.h"

int main() {
  float rate, motion_g, impact_g;
  if (std::scanf("%f %f %f", &rate, &motion_g, &impact_g) != 3)
    return 2;
  motion::Analyzer a;
  a.configure(rate, motion_g, impact_g);
  motion::Sample s;
  while (std::scanf("%f %f %f", &s.x, &s.y, &s.z) == 3) {
    motion::Result r = a.add(s);
    if (!r.moved && r.impact_g == 0) {
      std::printf("-\n");
      continue;
    }
    if (r.moved)
      std::printf("moved");
    if (r.impact_g > 0)
      std::printf("%simpact %.1f", r.moved ? " " : "", r.impact_g);
    std::printf("\n");
  }
  return 0;
}
