"""One car's trips: when one starts and stops, decided once from GNSS fixes and
the motion sensor, for every uplink (tracker_uplink) that reports them."""

import subprocess
from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import binary_sensor, sensor, time
from esphome.const import CONF_ID, CONF_MODEL, CONF_TEMPERATURE, CONF_TIME_ID
from esphome.core import CORE

DOMAIN = "trip_tracker"
CODEOWNERS = ["@raetha"]

CONF_TRIP_TRACKER_ID = "trip_tracker_id"
CONF_MOVING_SPEED = "moving_speed"
CONF_STOP_DELAY = "stop_delay"
CONF_START_FIXES = "start_fixes"
CONF_START_DISTANCE = "start_distance"
CONF_START_MOTION = "start_motion"
CONF_MOTION = "motion"
CONF_BATTERY_VOLTAGE = "battery_voltage"
CONF_SIGNAL = "signal"
CONF_IMPACT = "impact"
CONF_ON_TRIP_START = "on_trip_start"
CONF_ON_TRIP_STOP = "on_trip_stop"
CONF_ON_FIX = "on_fix"
CONF_ON_IMPACT = "on_impact"
CONF_FIRMWARE_VERSION = "firmware_version"

trip_tracker_ns = cg.esphome_ns.namespace("trip_tracker")
TripTracker = trip_tracker_ns.class_("TripTracker", cg.Component)
Fix = trip_tracker_ns.struct("Fix")
TripConfig = cg.global_ns.namespace("trackcodec").struct("TripConfig")
TripMovingCondition = trip_tracker_ns.class_(
    "TripMovingCondition", automation.Condition
)


def _seconds_or_off(value):
    if value in (0, "0", "0s", "off", False):
        return cv.TimePeriod(seconds=0)
    return cv.positive_time_period_seconds(value)


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(TripTracker),
        cv.Optional(CONF_MOVING_SPEED, default=6.0): cv.float_range(min=1, max=50),
        cv.Optional(CONF_STOP_DELAY, default="180s"): cv.positive_time_period_seconds,
        cv.Optional(CONF_START_FIXES, default=3): cv.int_range(min=1, max=30),
        cv.Optional(CONF_START_DISTANCE, default="50m"): cv.All(
            cv.distance, cv.Range(min=0, max=1000)
        ),
        # A trip starts only this soon after the motion sensor felt the car
        # move; 0s: GPS alone
        cv.Optional(CONF_START_MOTION, default="300s"): _seconds_or_off,
        cv.Optional(CONF_MOTION): cv.use_id(binary_sensor.BinarySensor),
        cv.Optional(CONF_BATTERY_VOLTAGE): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_TEMPERATURE): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_SIGNAL): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_IMPACT): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
        # Who the device is, for uplinks whose protocol carries it (the
        # webhook's, once per boot). The version defaults to the project's
        # (esphome: project: version) and the commit it was built from.
        cv.Optional(CONF_MODEL): cv.All(cv.string, cv.Length(max=32)),
        cv.Optional(CONF_FIRMWARE_VERSION): cv.All(cv.string, cv.Length(max=32)),
        cv.Optional(CONF_ON_TRIP_START): automation.validate_automation({}),
        cv.Optional(CONF_ON_TRIP_STOP): automation.validate_automation({}),
        cv.Optional(CONF_ON_FIX): automation.validate_automation({}),
        cv.Optional(CONF_ON_IMPACT): automation.validate_automation({}),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(
        var.set_config(
            cg.StructInitializer(
                TripConfig,
                ("moving_speed_kmh", config[CONF_MOVING_SPEED]),
                ("stop_delay_s", config[CONF_STOP_DELAY].total_seconds),
                ("start_fixes", config[CONF_START_FIXES]),
                ("start_distance_m", config[CONF_START_DISTANCE]),
            )
        )
    )
    cg.add(var.set_start_motion(config[CONF_START_MOTION].total_milliseconds))
    if CONF_MOTION in config:
        cg.add(var.set_motion(await cg.get_variable(config[CONF_MOTION])))
    for key, setter in (
        (CONF_BATTERY_VOLTAGE, var.set_battery),
        (CONF_TEMPERATURE, var.set_temperature),
        (CONF_SIGNAL, var.set_signal),
        (CONF_IMPACT, var.set_impact),
    ):
        if key in config:
            cg.add(setter(await cg.get_variable(config[key])))
    if CONF_TIME_ID in config:
        cg.add(var.set_time(await cg.get_variable(config[CONF_TIME_ID])))
    if CONF_MODEL in config:
        cg.add(var.set_model(config[CONF_MODEL]))
    firmware = config.get(CONF_FIRMWARE_VERSION, _firmware_version())
    if firmware:
        cg.add(var.set_firmware(firmware))
    for conf in config.get(CONF_ON_TRIP_START, []):
        await automation.build_automation(var.get_trip_start_trigger(), [], conf)
    for conf in config.get(CONF_ON_TRIP_STOP, []):
        await automation.build_automation(var.get_trip_stop_trigger(), [], conf)
    for conf in config.get(CONF_ON_FIX, []):
        await automation.build_automation(var.get_fix_trigger(), [(Fix, "fix")], conf)
    for conf in config.get(CONF_ON_IMPACT, []):
        await automation.build_automation(
            var.get_impact_trigger(), [(cg.float_, "peak_g")], conf
        )


def _firmware_version() -> str:
    """The project's version, with the commit of this component's checkout as
    semver build metadata ("0.1.0+ff6a429"; "-dirty" with local changes);
    "" without a project version."""
    version = CORE.config.get("esphome", {}).get("project", {}).get("version", "")
    if not version:
        return ""
    here = Path(__file__).resolve().parent
    try:
        commit = subprocess.run(
            ["git", "-C", str(here), "log", "-1", "--format=%h"],
            capture_output=True,
            text=True,
            timeout=10,
            check=True,
        ).stdout.strip()
        dirty = subprocess.run(
            ["git", "-C", str(here), "diff", "--quiet", "HEAD", "--", "."],
            capture_output=True,
            timeout=10,
            check=False,
        ).returncode
    except (OSError, subprocess.SubprocessError):
        return version
    if not commit:
        return version
    return f"{version}+{commit}{'-dirty' if dirty else ''}"[:32]


@automation.register_condition(
    "trip_tracker.is_moving",
    TripMovingCondition,
    cv.maybe_simple_value({cv.GenerateID(): cv.use_id(TripTracker)}, key=CONF_ID),
)
async def is_moving_to_code(config, condition_id, template_arg, args):
    var = cg.new_Pvariable(condition_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var
