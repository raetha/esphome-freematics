"""Send GNSS fixes to Traccar as compact Teltonika GH3000 records over UDP."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import sensor, socket, time
from esphome.const import CONF_ID, CONF_PORT, CONF_TIME_ID

CODEOWNERS = ["@raetha"]
DEPENDENCIES = ["network"]
AUTO_LOAD = ["socket"]

CONF_HOST = "host"
CONF_DEVICE_ID = "device_id"
CONF_SEND_INTERVAL = "send_interval"
CONF_ACK_TIMEOUT = "ack_timeout"
CONF_MAX_QUEUE = "max_queue"
CONF_BATCH_MAX = "batch_max"
CONF_BATTERY_VOLTAGE = "battery_voltage"
CONF_MOVING_SPEED = "moving_speed"
CONF_STOP_DELAY = "stop_delay"
CONF_MOVING_INTERVAL = "moving_interval"
CONF_TURN_ANGLE = "turn_angle"
CONF_MIN_TURN_INTERVAL = "min_turn_interval"
CONF_HEARTBEAT_INTERVAL = "heartbeat_interval"
CONF_ON_MOTION_START = "on_motion_start"
CONF_ON_MOTION_STOP = "on_motion_stop"

traccar_uplink_ns = cg.esphome_ns.namespace("traccar_uplink")
TraccarUplink = traccar_uplink_ns.class_("TraccarUplink", cg.Component)
FlushAction = traccar_uplink_ns.class_("FlushAction", automation.Action)
TrackerConfig = cg.global_ns.namespace("traccar_uplink").struct("TrackerConfig")


def _device_id(value):
    value = cv.string_strict(value)
    if not 1 <= len(value) <= 32 or not value.isprintable() or not value.isascii():
        raise cv.Invalid("device_id must be 1 to 32 printable ASCII characters")
    return value


def _seconds(value):
    return cv.positive_time_period_seconds(value)


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(TraccarUplink),
            cv.Required(CONF_HOST): cv.string_strict,
            cv.Optional(CONF_PORT, default=5027): cv.port,
            cv.Required(CONF_DEVICE_ID): _device_id,
            cv.Optional(CONF_SEND_INTERVAL, default="60s"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(min=cv.TimePeriod(seconds=5)),
            ),
            cv.Optional(CONF_ACK_TIMEOUT, default="15s"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(min=cv.TimePeriod(seconds=2)),
            ),
            cv.Optional(CONF_MAX_QUEUE, default=300): cv.int_range(min=10, max=2000),
            cv.Optional(CONF_BATCH_MAX, default=40): cv.int_range(min=1, max=40),
            cv.Optional(CONF_BATTERY_VOLTAGE): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
            cv.Optional(CONF_MOVING_SPEED, default=6.0): cv.float_range(min=1, max=50),
            cv.Optional(CONF_STOP_DELAY, default="180s"): _seconds,
            cv.Optional(CONF_MOVING_INTERVAL, default="10s"): _seconds,
            cv.Optional(CONF_TURN_ANGLE, default=30.0): cv.float_range(min=5, max=180),
            cv.Optional(CONF_MIN_TURN_INTERVAL, default="3s"): _seconds,
            cv.Optional(CONF_HEARTBEAT_INTERVAL, default="15min"): _seconds,
            cv.Optional(CONF_ON_MOTION_START): automation.validate_automation({}),
            cv.Optional(CONF_ON_MOTION_STOP): automation.validate_automation({}),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    socket.consume_sockets(1, "traccar_uplink", socket.SocketType.UDP),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_host(config[CONF_HOST]))
    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_device_id(config[CONF_DEVICE_ID]))
    cg.add(var.set_send_interval(config[CONF_SEND_INTERVAL]))
    cg.add(var.set_ack_timeout(config[CONF_ACK_TIMEOUT]))
    cg.add(var.set_max_queue(config[CONF_MAX_QUEUE]))
    cg.add(var.set_batch_max(config[CONF_BATCH_MAX]))
    tracker = cg.StructInitializer(
        TrackerConfig,
        ("moving_speed_kmh", config[CONF_MOVING_SPEED]),
        ("stop_delay_s", config[CONF_STOP_DELAY].total_seconds),
        ("moving_interval_s", config[CONF_MOVING_INTERVAL].total_seconds),
        ("turn_angle_deg", config[CONF_TURN_ANGLE]),
        ("min_turn_interval_s", config[CONF_MIN_TURN_INTERVAL].total_seconds),
        ("heartbeat_s", config[CONF_HEARTBEAT_INTERVAL].total_seconds),
    )
    cg.add(var.set_tracker_config(tracker))
    if CONF_BATTERY_VOLTAGE in config:
        cg.add(
            var.set_battery_sensor(await cg.get_variable(config[CONF_BATTERY_VOLTAGE]))
        )
    if CONF_TIME_ID in config:
        cg.add(var.set_time(await cg.get_variable(config[CONF_TIME_ID])))
    for conf in config.get(CONF_ON_MOTION_START, []):
        await automation.build_automation(var.get_motion_start_trigger(), [], conf)
    for conf in config.get(CONF_ON_MOTION_STOP, []):
        await automation.build_automation(var.get_motion_stop_trigger(), [], conf)


@automation.register_action(
    "traccar_uplink.flush",
    FlushAction,
    cv.Schema({cv.GenerateID(): cv.use_id(TraccarUplink)}),
    synchronous=True,
)
async def flush_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var
