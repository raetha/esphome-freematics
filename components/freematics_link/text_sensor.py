"""The Freematics co-processor's version, from its ATI reply."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import CONF_VERSION, ENTITY_CATEGORY_DIAGNOSTIC

from . import CONF_FREEMATICS_LINK_ID, FreematicsLink

DEPENDENCIES = ["freematics_link"]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_FREEMATICS_LINK_ID): cv.use_id(FreematicsLink),
        cv.Optional(CONF_VERSION): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_FREEMATICS_LINK_ID])
    if conf := config.get(CONF_VERSION):
        cg.add(parent.set_version_text_sensor(await text_sensor.new_text_sensor(conf)))
