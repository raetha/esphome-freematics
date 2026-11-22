"""The ICM-426xx's motion binary sensor: on while the device is being moved."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import DEVICE_CLASS_MOVING

from . import CONF_ICM426XX_ID, ICM426xx

DEPENDENCIES = ["icm426xx"]

CONF_MOTION = "motion"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ICM426XX_ID): cv.use_id(ICM426xx),
        cv.Optional(CONF_MOTION): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_MOVING
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_ICM426XX_ID])
    if conf := config.get(CONF_MOTION):
        cg.add(
            parent.set_motion_binary_sensor(await binary_sensor.new_binary_sensor(conf))
        )
