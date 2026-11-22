"""The ICM-426xx's sensors: the chip's temperature, and each impact's peak."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_TEMPERATURE,
    DEVICE_CLASS_TEMPERATURE,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
)

from . import CONF_ICM426XX_ID, ICM426xx

DEPENDENCIES = ["icm426xx"]

CONF_IMPACT = "impact"
UNIT_G = "g"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ICM426XX_ID): cv.use_id(ICM426xx),
        cv.Optional(CONF_TEMPERATURE): sensor.sensor_schema(
            unit_of_measurement=UNIT_CELSIUS,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_TEMPERATURE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        # Published once per impact: its peak beyond gravity
        cv.Optional(CONF_IMPACT): sensor.sensor_schema(
            unit_of_measurement=UNIT_G, accuracy_decimals=1, icon="mdi:car-emergency"
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_ICM426XX_ID])
    if conf := config.get(CONF_TEMPERATURE):
        cg.add(parent.set_temperature_sensor(await sensor.new_sensor(conf)))
    if conf := config.get(CONF_IMPACT):
        cg.add(parent.set_impact_sensor(await sensor.new_sensor(conf)))
