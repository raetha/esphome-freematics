"""Write the device's log to a file, normally on an SD card (sd_card), so what
happened away from Wi-Fi can be read later from the card."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import logger
from esphome.const import CONF_ID, CONF_LEVEL, CONF_PATH

CODEOWNERS = ["@raetha"]
DEPENDENCIES = ["logger"]
DOMAIN = "file_logger"

CONF_MAX_SIZE = "max_size"
CONF_BUFFER_SIZE = "buffer_size"
CONF_FLUSH_INTERVAL = "flush_interval"

file_logger_ns = cg.esphome_ns.namespace("file_logger")
FileLogger = file_logger_ns.class_("FileLogger", cg.Component)


def _path(value):
    value = cv.string_strict(value)
    if not value.startswith("/") or value.endswith("/") or len(value) > 60:
        raise cv.Invalid("Use a full file path such as /sdcard/esphome.log")
    return value


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(FileLogger),
        cv.Required(CONF_PATH): _path,
        cv.Optional(CONF_LEVEL, default="DEBUG"): logger.is_log_level,
        cv.Optional(CONF_MAX_SIZE, default="512kB"): cv.All(
            cv.validate_bytes, cv.Range(min=4096)
        ),
        cv.Optional(CONF_BUFFER_SIZE, default=4096): cv.int_range(min=1024, max=65536),
        cv.Optional(
            CONF_FLUSH_INTERVAL, default="10s"
        ): cv.positive_time_period_milliseconds,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    logger.request_log_listener()
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_path(config[CONF_PATH]))
    cg.add(var.set_level(logger.LOG_LEVELS[config[CONF_LEVEL]]))
    cg.add(var.set_max_size(config[CONF_MAX_SIZE]))
    cg.add(var.set_buffer_size(config[CONF_BUFFER_SIZE]))
    cg.add(var.set_flush_interval(config[CONF_FLUSH_INTERVAL]))
