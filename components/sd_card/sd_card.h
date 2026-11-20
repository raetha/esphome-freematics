#pragma once

#include "esphome/core/component.h"
#include "esphome/components/spi/spi.h"

#include <sdmmc_cmd.h>

namespace esphome::sd_card {

/// Mounts a microSD card's FAT file system at a VFS path, over an ESPHome SPI
/// bus. Mounting happens once, in setup(); files are then opened with the
/// standard C functions under get_mount_point().
class SdCard final : public Component {
 public:
  void setup() override;
  void dump_config() override;
  // After the SPI bus, before the components that keep files on the card.
  float get_setup_priority() const override { return setup_priority::BUS - 10.0f; }

  void set_spi_parent(spi::SPIComponent *parent) { this->parent_ = parent; }
  void set_cs_pin(uint8_t pin) { this->cs_pin_ = pin; }
  void set_mount_point(const char *path) { this->mount_point_ = path; }
  void set_frequency_khz(uint32_t khz) { this->frequency_khz_ = khz; }
  void set_format_if_mount_failed(bool format) { this->format_if_mount_failed_ = format; }
  void set_max_files(uint8_t n) { this->max_files_ = n; }

  bool is_mounted() const { return this->card_ != nullptr; }
  const char *get_mount_point() const { return this->mount_point_; }
  /// Card capacity in bytes; 0 when not mounted.
  uint64_t get_capacity() const;

 protected:
  spi::SPIComponent *parent_{nullptr};
  sdmmc_card_t *card_{nullptr};
  const char *mount_point_{"/sdcard"};
  uint32_t frequency_khz_{20000};
  uint8_t cs_pin_{0};
  uint8_t max_files_{4};
  bool format_if_mount_failed_{false};
};

}  // namespace esphome::sd_card
