"""Write the device's log to a file, normally on an SD card (sd_card), so what
happened away from Wi-Fi can be read later from the card, or downloaded over
the local network (http:, on ESPHome's web_server_base, behind a password)."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import logger, web_server_base
from esphome.const import CONF_ID, CONF_LEVEL, CONF_PASSWORD, CONF_PATH, CONF_USERNAME

CODEOWNERS = ["@raetha"]
DEPENDENCIES = ["logger"]
DOMAIN = "file_logger"

CONF_MAX_SIZE = "max_size"
CONF_BUFFER_SIZE = "buffer_size"
CONF_FLUSH_INTERVAL = "flush_interval"
CONF_HTTP = "http"


def _http_clients(config):
    if CONF_HTTP in config:
        # One download at a time, on the shared HTTP server
        web_server_base.add_httpd_clients(1, DOMAIN)
    return config


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
        # Serve the files at /log and /log.1 on the local network
        cv.Optional(CONF_HTTP): cv.Schema(
            {
                cv.GenerateID(web_server_base.CONF_WEB_SERVER_BASE_ID): cv.use_id(
                    web_server_base.WebServerBase
                ),
                cv.Required(CONF_USERNAME): cv.All(cv.string_strict, cv.Length(min=1)),
                cv.Required(CONF_PASSWORD): cv.All(cv.string_strict, cv.Length(min=8)),
            }
        ),
    }
).extend(cv.COMPONENT_SCHEMA)
# The writer is a FreeRTOS task of its own; the download, ESP-IDF's HTTP server.
CONFIG_SCHEMA = cv.All(CONFIG_SCHEMA, cv.only_on_esp32, _http_clients)


async def to_code(config):
    logger.request_log_listener()
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_path(config[CONF_PATH]))
    cg.add(var.set_level(logger.LOG_LEVELS[config[CONF_LEVEL]]))
    cg.add(var.set_max_size(config[CONF_MAX_SIZE]))
    cg.add(var.set_buffer_size(config[CONF_BUFFER_SIZE]))
    cg.add(var.set_flush_interval(config[CONF_FLUSH_INTERVAL]))
    if http := config.get(CONF_HTTP):
        base = await cg.get_variable(http[web_server_base.CONF_WEB_SERVER_BASE_ID])
        cg.add_define("USE_FILE_LOGGER_HTTP")
        cg.add_define("USE_WEBSERVER_AUTH")
        cg.add(base.set_auth_username(http[CONF_USERNAME]))
        cg.add(base.set_auth_password(http[CONF_PASSWORD]))
        cg.add(var.set_web_server(base))
