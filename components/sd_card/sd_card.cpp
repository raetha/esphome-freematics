#include "sd_card.h"

#include "esphome/core/log.h"

#include <driver/sdspi_host.h>
#include <esp_vfs_fat.h>

namespace esphome::sd_card {

static const char *const TAG = "sd_card";

void SdCard::setup() {
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = this->parent_->get_interface();
  host.max_freq_khz = static_cast<int>(this->frequency_khz_);

  sdspi_device_config_t device = SDSPI_DEVICE_CONFIG_DEFAULT();
  device.host_id = this->parent_->get_interface();
  device.gpio_cs = static_cast<gpio_num_t>(this->cs_pin_);

  esp_vfs_fat_sdmmc_mount_config_t mount{};
  mount.format_if_mount_failed = this->format_if_mount_failed_;
  mount.max_files = this->max_files_;
  mount.allocation_unit_size = 16 * 1024;

  esp_err_t err = esp_vfs_fat_sdspi_mount(this->mount_point_, &host, &device, &mount, &this->card_);
  if (err != ESP_OK) {
    this->card_ = nullptr;
    ESP_LOGW(TAG, "No card mounted at %s: %s", this->mount_point_, esp_err_to_name(err));
    this->status_set_warning(LOG_STR("No SD card"));
  }
}

uint64_t SdCard::get_capacity() const {
  if (this->card_ == nullptr)
    return 0;
  return static_cast<uint64_t>(this->card_->csd.capacity) * this->card_->csd.sector_size;
}

void SdCard::dump_config() {
  ESP_LOGCONFIG(TAG,
                "SD card:\n"
                "  Mount point: %s\n"
                "  CS pin: GPIO%u\n"
                "  Frequency: %u kHz",
                this->mount_point_, this->cs_pin_, (unsigned) this->frequency_khz_);
  if (this->card_ == nullptr) {
    ESP_LOGCONFIG(TAG, "  Not mounted");
    return;
  }
  ESP_LOGCONFIG(TAG,
                "  Card: %s, %u MB\n"
                "  Speed: %d kHz",
                this->card_->cid.name, (unsigned) (this->get_capacity() / (1024 * 1024)), this->card_->real_freq_khz);
}

}  // namespace esphome::sd_card
