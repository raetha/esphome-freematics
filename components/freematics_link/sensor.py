"""Sensors from the Freematics co-processor: GNSS position and battery voltage."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_ALTITUDE,
    CONF_BATTERY_VOLTAGE,
    CONF_COURSE,
    CONF_LATITUDE,
    CONF_LONGITUDE,
    CONF_SATELLITES,
    CONF_SPEED,
    DEVICE_CLASS_DISTANCE,
    DEVICE_CLASS_SPEED,
    DEVICE_CLASS_VOLTAGE,
    STATE_CLASS_MEASUREMENT,
    UNIT_DEGREES,
    UNIT_KILOMETER_PER_HOUR,
    UNIT_METER,
    UNIT_VOLT,
)

from . import CONF_FREEMATICS_LINK_ID, FreematicsLink

DEPENDENCIES = ["freematics_link"]

CONF_HDOP = "hdop"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_FREEMATICS_LINK_ID): cv.use_id(FreematicsLink),
        cv.Optional(CONF_LATITUDE): sensor.sensor_schema(
            unit_of_measurement=UNIT_DEGREES, accuracy_decimals=6
        ),
        cv.Optional(CONF_LONGITUDE): sensor.sensor_schema(
            unit_of_measurement=UNIT_DEGREES, accuracy_decimals=6
        ),
        cv.Optional(CONF_ALTITUDE): sensor.sensor_schema(
            unit_of_measurement=UNIT_METER,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_DISTANCE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_SPEED): sensor.sensor_schema(
            unit_of_measurement=UNIT_KILOMETER_PER_HOUR,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_SPEED,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_COURSE): sensor.sensor_schema(
            unit_of_measurement=UNIT_DEGREES, accuracy_decimals=0
        ),
        cv.Optional(CONF_SATELLITES): sensor.sensor_schema(
            accuracy_decimals=0, state_class=STATE_CLASS_MEASUREMENT
        ),
        cv.Optional(CONF_HDOP): sensor.sensor_schema(
            accuracy_decimals=0, state_class=STATE_CLASS_MEASUREMENT
        ),
        cv.Optional(CONF_BATTERY_VOLTAGE): sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT,
            accuracy_decimals=2,
            device_class=DEVICE_CLASS_VOLTAGE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_FREEMATICS_LINK_ID])
    for key, setter in (
        (CONF_LATITUDE, "set_latitude_sensor"),
        (CONF_LONGITUDE, "set_longitude_sensor"),
        (CONF_ALTITUDE, "set_altitude_sensor"),
        (CONF_SPEED, "set_speed_sensor"),
        (CONF_COURSE, "set_course_sensor"),
        (CONF_SATELLITES, "set_satellites_sensor"),
        (CONF_HDOP, "set_hdop_sensor"),
        (CONF_BATTERY_VOLTAGE, "set_battery_voltage_sensor"),
    ):
        if conf := config.get(key):
            cg.add(getattr(parent, setter)(await sensor.new_sensor(conf)))
