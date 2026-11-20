"""Whether the Freematics co-processor's GNSS has a usable fix."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor

from . import CONF_FREEMATICS_LINK_ID, FreematicsLink

DEPENDENCIES = ["freematics_link"]

CONF_FIX = "fix"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_FREEMATICS_LINK_ID): cv.use_id(FreematicsLink),
        cv.Optional(CONF_FIX): binary_sensor.binary_sensor_schema(),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_FREEMATICS_LINK_ID])
    if conf := config.get(CONF_FIX):
        cg.add(
            parent.set_fix_binary_sensor(await binary_sensor.new_binary_sensor(conf))
        )
