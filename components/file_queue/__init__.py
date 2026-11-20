"""A first-in, first-out queue of fixed-size records in a file, kept across
reboots: on an SD card (sd_card) or any other mounted file system.

The component that keeps records in it (the queue's owner) takes the queue's
id and declares the record size and layout version from its own ``to_code``:
``cg.add(queue.set_record_layout(size, version))``. A file made for another
layout is started afresh.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_PATH

CODEOWNERS = ["@raetha"]
DOMAIN = "file_queue"
MULTI_CONF = True

CONF_CAPACITY = "capacity"

file_queue_ns = cg.esphome_ns.namespace("file_queue")
FileQueue = file_queue_ns.class_("FileQueue", cg.Component)


def _path(value):
    value = cv.string_strict(value)
    if not value.startswith("/") or value.endswith("/"):
        raise cv.Invalid("Use a full file path such as /sdcard/queue.bin")
    return value


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(FileQueue),
        cv.Required(CONF_PATH): _path,
        cv.Optional(CONF_CAPACITY, default=10000): cv.int_range(min=1, max=10_000_000),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_path(config[CONF_PATH]))
    cg.add(var.set_capacity(config[CONF_CAPACITY]))
    cg.add_define("USE_FILE_QUEUE")
