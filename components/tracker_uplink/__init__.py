"""Send GNSS fixes to Traccar as compact Teltonika GH3000 records over UDP, or
to a Home Assistant webhook as compact batches over HTTPS."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import esp32, sensor, socket, time
from esphome.const import CONF_ID, CONF_PORT, CONF_TIME_ID, CONF_URL
from esphome.core import CORE

DOMAIN = "tracker_uplink"
CODEOWNERS = ["@raetha"]
DEPENDENCIES = ["network"]
MULTI_CONF = True  # Traccar and Home Assistant together
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
CONF_START_FIXES = "start_fixes"
CONF_ALTITUDE = "altitude"
CONF_START_DISTANCE = "start_distance"
CONF_ON_MOTION_START = "on_motion_start"
CONF_ON_MOTION_STOP = "on_motion_stop"
CONF_FILE_QUEUE_ID = "file_queue_id"
CONF_PROTOCOL = "protocol"
PROTOCOLS = {"teltonika": 5027, "freematics": 5170, "home_assistant": None}
WEBHOOK = "home_assistant"

# Bump when trackcodec::Record's layout changes, so stored files start afresh.
RECORD_FORMAT = 3

tracker_uplink_ns = cg.esphome_ns.namespace("tracker_uplink")
TrackerUplink = tracker_uplink_ns.class_("TrackerUplink", cg.Component)
FlushAction = tracker_uplink_ns.class_("FlushAction", automation.Action)
TrackerConfig = cg.global_ns.namespace("trackcodec").struct("TrackerConfig")
FileQueue = cg.esphome_ns.namespace("file_queue").class_("FileQueue", cg.Component)


def _device_id(value):
    value = cv.string_strict(value)
    if not 1 <= len(value) <= 32 or not value.isprintable() or not value.isascii():
        raise cv.Invalid("device_id must be 1 to 32 printable ASCII characters")
    return value


def _seconds(value):
    return cv.positive_time_period_seconds(value)


def _protocol_fields(config):
    if config[CONF_PROTOCOL] == WEBHOOK:
        if CONF_URL not in config:
            raise cv.Invalid("the home_assistant protocol needs url (the webhook)")
        if not CORE.is_esp32:
            raise cv.Invalid("the home_assistant protocol needs an ESP32")
        for key in (CONF_HOST, CONF_PORT, CONF_DEVICE_ID):
            if key in config:
                raise cv.Invalid(
                    f"{key} is for Traccar, not the home_assistant protocol"
                )
    else:
        for key in (CONF_HOST, CONF_DEVICE_ID):
            if key not in config:
                raise cv.Invalid(f"{key} is required for Traccar")
        if CONF_URL in config:
            raise cv.Invalid("url is for the home_assistant protocol")
    return config


def _url(value):
    value = cv.string_strict(value)
    if not value.startswith("https://"):
        raise cv.Invalid("the webhook URL must start with https://")
    return value


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(TrackerUplink),
            cv.Optional(CONF_HOST): cv.string_strict,
            cv.Optional(CONF_URL): _url,
            cv.Optional(CONF_PROTOCOL, default="teltonika"): cv.one_of(
                *PROTOCOLS, lower=True
            ),
            cv.Optional(CONF_PORT): cv.port,
            cv.Optional(CONF_DEVICE_ID): _device_id,
            cv.Optional(CONF_SEND_INTERVAL, default="60s"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(min=cv.TimePeriod(seconds=5)),
            ),
            cv.Optional(CONF_ACK_TIMEOUT, default="15s"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(min=cv.TimePeriod(seconds=2)),
            ),
            cv.Optional(CONF_MAX_QUEUE, default=300): cv.int_range(min=10, max=2000),
            cv.Optional(CONF_BATCH_MAX, default=40): cv.int_range(min=1, max=80),
            cv.Optional(CONF_BATTERY_VOLTAGE): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
            cv.Optional(CONF_FILE_QUEUE_ID): cv.use_id(FileQueue),
            cv.Optional(CONF_MOVING_SPEED, default=6.0): cv.float_range(min=1, max=50),
            cv.Optional(CONF_STOP_DELAY, default="180s"): _seconds,
            cv.Optional(CONF_MOVING_INTERVAL, default="10s"): _seconds,
            cv.Optional(CONF_TURN_ANGLE, default=30.0): cv.float_range(min=5, max=180),
            cv.Optional(CONF_MIN_TURN_INTERVAL, default="3s"): _seconds,
            cv.Optional(CONF_HEARTBEAT_INTERVAL, default="15min"): _seconds,
            # 2 bytes per located record (about 7% more data while driving)
            cv.Optional(CONF_ALTITUDE, default=True): cv.boolean,
            cv.Optional(CONF_START_FIXES, default=3): cv.int_range(min=1, max=30),
            cv.Optional(CONF_START_DISTANCE, default="50m"): cv.All(
                cv.distance, cv.Range(min=0, max=1000)
            ),
            cv.Optional(CONF_ON_MOTION_START): automation.validate_automation({}),
            cv.Optional(CONF_ON_MOTION_STOP): automation.validate_automation({}),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _protocol_fields,
    socket.consume_sockets(1, "tracker_uplink", socket.SocketType.UDP),
    socket.consume_sockets(1, "tracker_uplink", socket.SocketType.TCP),
)


async def to_code(config):
    socket.require_ipv4_resolve()  # compiles the async DNS lookup we use
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    if config[CONF_PROTOCOL] == WEBHOOK:
        cg.add(var.set_webhook_url(config[CONF_URL]))
        cg.add_define("USE_UPLINK_WEBHOOK")
        esp32.request_http_client()
        esp32.require_certificate_bundle()
        # A TLS session ticket lets the next batch skip most of the handshake
        # when the server has closed the connection in between.
        esp32.require_mbedtls_tls_extras(["CONFIG_MBEDTLS_CLIENT_SSL_SESSION_TICKETS"])
        esp32.add_idf_sdkconfig_option("CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS", True)
    else:
        cg.add(var.set_host(config[CONF_HOST]))
        cg.add(var.set_port(config.get(CONF_PORT, PROTOCOLS[config[CONF_PROTOCOL]])))
        cg.add(var.set_device_id(config[CONF_DEVICE_ID]))
    cg.add(var.set_freematics_protocol(config[CONF_PROTOCOL] == "freematics"))
    cg.add(var.set_altitude(config[CONF_ALTITUDE]))
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
        ("start_fixes", config[CONF_START_FIXES]),
        ("start_distance_m", config[CONF_START_DISTANCE]),
    )
    cg.add(var.set_tracker_config(tracker))
    if CONF_BATTERY_VOLTAGE in config:
        cg.add(
            var.set_battery_sensor(await cg.get_variable(config[CONF_BATTERY_VOLTAGE]))
        )
    if CONF_TIME_ID in config:
        cg.add(var.set_time(await cg.get_variable(config[CONF_TIME_ID])))
    if CONF_FILE_QUEUE_ID in config:
        queue = await cg.get_variable(config[CONF_FILE_QUEUE_ID])
        record_size = cg.RawExpression("sizeof(::trackcodec::Record)")
        cg.add(queue.set_record_layout(record_size, RECORD_FORMAT))
        cg.add(var.set_file_queue(queue))
    for conf in config.get(CONF_ON_MOTION_START, []):
        await automation.build_automation(var.get_motion_start_trigger(), [], conf)
    for conf in config.get(CONF_ON_MOTION_STOP, []):
        await automation.build_automation(var.get_motion_stop_trigger(), [], conf)


@automation.register_action(
    "tracker_uplink.flush",
    FlushAction,
    cv.maybe_simple_value({cv.GenerateID(): cv.use_id(TrackerUplink)}, key=CONF_ID),
    synchronous=True,
)
async def flush_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var
