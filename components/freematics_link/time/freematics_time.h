#pragma once

#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/helpers.h"

#include "../freematics_link.h"

namespace esphome::freematics_link {

/// Sets the clock from the latest GNSS fix's date and time.
class FreematicsTime final : public time::RealTimeClock, public Parented<FreematicsLink> {
 public:
  void update() override {
    if (!this->parent_->has_fix())
      return;
    uint32_t epoch = this->parent_->last_fix().epoch;
    if (epoch != 0)
      this->synchronize_epoch_(epoch);
  }
};

}  // namespace esphome::freematics_link
