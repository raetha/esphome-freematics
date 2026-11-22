#pragma once
// What the accelerometer's samples mean for a car: whether it's being moved
// (getting in, driving) and whether it took an impact. No ESPHome
// dependencies: tests/cpp builds it on the host.

#include <cmath>
#include <cstdint>

namespace motion {

struct Sample {
  float x, y, z;  // g
};

struct Result {
  bool moved{false};     // an axis changed by more than the motion threshold since the last sample
  float impact_g{0.0f};  // an impact just ended: its peak, without gravity; 0: none
};

class Analyzer {
 public:
  /// rate_hz: samples a second; motion_g: change between samples that counts
  /// as movement; impact_g: acceleration beyond gravity that counts as an impact.
  void configure(float rate_hz, float motion_g, float impact_g) {
    this->rate_hz_ = rate_hz;
    this->motion_g_ = motion_g;
    this->impact_g_ = impact_g;
    // Gravity follows the dongle's orientation over about a second.
    this->alpha_ = 1.0f / rate_hz;
  }

  /// Forget the last sample and gravity (the sensor was reconfigured).
  void reset() {
    this->has_last_ = false;
    this->in_impact_ = false;
    this->cooldown_ = 0;
  }

  Result add(const Sample &s) {
    Result r;
    if (!this->has_last_) {
      this->last_ = s;
      this->gravity_ = s;
      this->has_last_ = true;
      return r;
    }
    r.moved = std::fabs(s.x - this->last_.x) > this->motion_g_ || std::fabs(s.y - this->last_.y) > this->motion_g_ ||
              std::fabs(s.z - this->last_.z) > this->motion_g_;
    this->last_ = s;

    float dx = s.x - this->gravity_.x, dy = s.y - this->gravity_.y, dz = s.z - this->gravity_.z;
    float dynamic = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (this->in_impact_) {
      // The peak of the next half second.
      if (dynamic > this->peak_)
        this->peak_ = dynamic;
      if (--this->left_ == 0) {
        this->in_impact_ = false;
        this->cooldown_ = static_cast<uint32_t>(this->rate_hz_ * 5);  // one impact, not its echoes
        r.impact_g = this->peak_;
      }
      return r;  // gravity isn't learnt from an impact
    }
    if (this->cooldown_ > 0) {
      this->cooldown_--;
    } else if (this->impact_g_ > 0 && dynamic >= this->impact_g_) {
      this->in_impact_ = true;
      this->peak_ = dynamic;
      this->left_ = static_cast<uint32_t>(this->rate_hz_ / 2);
      if (this->left_ == 0)
        this->left_ = 1;
      return r;
    }
    this->gravity_.x += (s.x - this->gravity_.x) * this->alpha_;
    this->gravity_.y += (s.y - this->gravity_.y) * this->alpha_;
    this->gravity_.z += (s.z - this->gravity_.z) * this->alpha_;
    return r;
  }

 protected:
  float rate_hz_{100};
  float motion_g_{0.1f};
  float impact_g_{4.0f};
  float alpha_{0.01f};
  bool has_last_{false};
  Sample last_{};
  Sample gravity_{};
  bool in_impact_{false};
  float peak_{0};
  uint32_t left_{0};
  uint32_t cooldown_{0};
};

}  // namespace motion
