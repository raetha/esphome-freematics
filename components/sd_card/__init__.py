"""Mount a microSD card's FAT file system over an SPI bus (ESP32).

Other components then use the card through ordinary file functions under
``mount_point`` (fopen("/sdcard/log.bin", ...)).
"""

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome import pins
from esphome.components import esp32, spi
from esphome.components.spi import CONF_INTERFACE_INDEX
from esphome.const import CONF_CS_PIN, CONF_FREQUENCY, CONF_ID, CONF_SPI_ID

CODEOWNERS = ["@raetha"]
DEPENDENCIES = ["spi"]
DOMAIN = "sd_card"
MULTI_CONF = True

CONF_MOUNT_POINT = "mount_point"
CONF_FORMAT_IF_MOUNT_FAILED = "format_if_mount_failed"
CONF_MAX_FILES = "max_files"

# ESP-IDF components ESPHome leaves out by default that the SD SPI driver and FAT need
IDF_COMPONENTS = (
    "fatfs",
    "wear_levelling",
    "sdmmc",
    "esp_driver_sdmmc",
    "esp_driver_sdspi",
)

sd_card_ns = cg.esphome_ns.namespace("sd_card")
SdCard = sd_card_ns.class_("SdCard", cg.Component)


def _mount_point(value):
    value = cv.string_strict(value)
    if not value.startswith("/") or len(value) < 2 or "/" in value[1:]:
        raise cv.Invalid("Use a top-level path such as /sdcard")
    if len(value) > 15:
        raise cv.Invalid("At most 15 characters (ESP-IDF's VFS prefix limit)")
    return value


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SdCard),
            cv.Required(CONF_SPI_ID): cv.use_id(spi.SPIComponent),
            cv.Required(CONF_CS_PIN): pins.internal_gpio_output_pin_number,
            cv.Optional(CONF_MOUNT_POINT, default="/sdcard"): _mount_point,
            cv.Optional(CONF_FREQUENCY, default="20MHz"): cv.All(
                cv.frequency, cv.Range(min=400e3, max=20e6)
            ),
            cv.Optional(CONF_FORMAT_IF_MOUNT_FAILED, default=False): cv.boolean,
            cv.Optional(CONF_MAX_FILES, default=4): cv.int_range(min=1, max=16),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on_esp32,
)


def _final_validate(config):
    spi.final_validate_device_schema(DOMAIN, require_mosi=True, require_miso=True)(
        config
    )
    # The SD SPI driver attaches to the bus's hardware host.
    cv.Schema(
        {
            cv.Required(CONF_SPI_ID): fv.id_declaration_match_schema(
                {
                    cv.Required(
                        CONF_INTERFACE_INDEX,
                        msg="sd_card needs this spi bus to use a hardware interface",
                    ): cv.valid
                }
            )
        },
        extra=cv.ALLOW_EXTRA,
    )(config)
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_spi_parent(await cg.get_variable(config[CONF_SPI_ID])))
    cg.add(var.set_cs_pin(config[CONF_CS_PIN]))
    cg.add(var.set_mount_point(config[CONF_MOUNT_POINT]))
    cg.add(var.set_frequency_khz(int(config[CONF_FREQUENCY] // 1000)))
    cg.add(var.set_format_if_mount_failed(config[CONF_FORMAT_IF_MOUNT_FAILED]))
    cg.add(var.set_max_files(config[CONF_MAX_FILES]))
    for name in IDF_COMPONENTS:
        esp32.include_builtin_idf_component(name)
    esp32.require_fatfs()
    cg.add_define("USE_SD_CARD")
