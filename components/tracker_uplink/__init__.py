"""Report one car's trips (trip_tracker) to a server: Traccar over UDP
(Teltonika GH3000, or its Freematics text protocol), or Home Assistant's
Freematics integration through a webhook (compact batches over HTTPS).

Each uplink samples the trips for its server (how often to record while
moving, heartbeats while parked), keeps the records until the server has them,
and sends them in batches through its protocol's transport (transports.py).
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.const import CONF_ID

from .transports import PROTOCOLS, tracker_uplink_ns

DOMAIN = "tracker_uplink"
CODEOWNERS = ["@raetha"]
DEPENDENCIES = ["network", "trip_tracker"]
AUTO_LOAD = ["socket"]
MULTI_CONF = True  # Traccar and Home Assistant together

CONF_PROTOCOL = "protocol"
CONF_SEND_INTERVAL = "send_interval"
CONF_MAX_QUEUE = "max_queue"
CONF_BATCH_MAX = "batch_max"
CONF_MOVING_INTERVAL = "moving_interval"
CONF_TURN_ANGLE = "turn_angle"
CONF_MIN_TURN_INTERVAL = "min_turn_interval"
CONF_HEARTBEAT_INTERVAL = "heartbeat_interval"
CONF_ALTITUDE = "altitude"
CONF_FILE_QUEUE_ID = "file_queue_id"
CONF_TRIP_TRACKER_ID = "trip_tracker_id"

# Bump when trackcodec::Record's layout changes, so stored files start afresh.
RECORD_FORMAT = 4

TrackerUplink = tracker_uplink_ns.class_("TrackerUplink", cg.Component)
FlushAction = tracker_uplink_ns.class_("FlushAction", automation.Action)
SampleConfig = cg.global_ns.namespace("trackcodec").struct("SampleConfig")
FileQueue = cg.esphome_ns.namespace("file_queue").class_("FileQueue", cg.Component)
TripTracker = cg.esphome_ns.namespace("trip_tracker").class_(
    "TripTracker", cg.Component
)


def _seconds(value):
    return cv.positive_time_period_seconds(value)


BASE_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(TrackerUplink),
        cv.GenerateID(CONF_TRIP_TRACKER_ID): cv.use_id(TripTracker),
        cv.Optional(CONF_SEND_INTERVAL, default="60s"): cv.All(
            cv.positive_time_period_milliseconds,
            cv.Range(min=cv.TimePeriod(seconds=5)),
        ),
        cv.Optional(CONF_MAX_QUEUE, default=300): cv.int_range(min=10, max=2000),
        cv.Optional(CONF_BATCH_MAX, default=40): cv.int_range(min=1, max=80),
        cv.Optional(CONF_FILE_QUEUE_ID): cv.use_id(FileQueue),
        cv.Optional(CONF_MOVING_INTERVAL, default="10s"): _seconds,
        cv.Optional(CONF_TURN_ANGLE, default=30.0): cv.float_range(min=5, max=180),
        cv.Optional(CONF_MIN_TURN_INTERVAL, default="3s"): _seconds,
        cv.Optional(CONF_HEARTBEAT_INTERVAL, default="15min"): _seconds,
        # Altitude: a byte or two per located record
        cv.Optional(CONF_ALTITUDE, default=True): cv.boolean,
    }
).extend(cv.COMPONENT_SCHEMA)

CONFIG_SCHEMA = cv.All(
    cv.typed_schema(
        {
            name: BASE_SCHEMA.extend(protocol.schema)
            for name, protocol in PROTOCOLS.items()
        },
        key=CONF_PROTOCOL,
        default_type="teltonika",
        lower=True,
    ),
    lambda config: PROTOCOLS[config[CONF_PROTOCOL]].validate(config),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_TRIP_TRACKER_ID])
    cg.add(var.set_parent(parent))
    cg.add(parent.add_listener(var))
    transport = await PROTOCOLS[config[CONF_PROTOCOL]].to_code(config)
    cg.add(var.set_transport(transport))
    cg.add(var.set_altitude(config[CONF_ALTITUDE]))
    cg.add(var.set_send_interval(config[CONF_SEND_INTERVAL]))
    cg.add(var.set_max_queue(config[CONF_MAX_QUEUE]))
    cg.add(var.set_batch_max(config[CONF_BATCH_MAX]))
    cg.add(
        var.set_sample_config(
            cg.StructInitializer(
                SampleConfig,
                ("moving_interval_s", config[CONF_MOVING_INTERVAL].total_seconds),
                ("turn_angle_deg", config[CONF_TURN_ANGLE]),
                ("min_turn_interval_s", config[CONF_MIN_TURN_INTERVAL].total_seconds),
                ("heartbeat_s", config[CONF_HEARTBEAT_INTERVAL].total_seconds),
            )
        )
    )
    if CONF_FILE_QUEUE_ID in config:
        queue = await cg.get_variable(config[CONF_FILE_QUEUE_ID])
        record_size = cg.RawExpression("sizeof(::trackcodec::Record)")
        cg.add(queue.set_record_layout(record_size, RECORD_FORMAT))
        cg.add(var.set_file_queue(queue))


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
