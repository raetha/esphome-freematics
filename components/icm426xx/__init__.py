"""TDK ICM-426xx motion sensors (the ICM-42627 on the Freematics ONE+ Model B).

Awake: movement (a binary sensor), impacts (a sensor of each one's peak) and
the chip's temperature, from 100 accelerometer samples a second. Before deep
sleep, icm426xx.arm_wake leaves it in low-power wake-on-motion.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import i2c
from esphome.const import CONF_ID

DOMAIN = "icm426xx"
CODEOWNERS = ["@raetha"]
DEPENDENCIES = ["i2c"]

CONF_ICM426XX_ID = "icm426xx_id"
CONF_MOTION_THRESHOLD = "motion_threshold"
CONF_IMPACT_THRESHOLD = "impact_threshold"
CONF_MOTION_HOLD = "motion_hold"
CONF_THRESHOLD = "threshold"

icm426xx_ns = cg.esphome_ns.namespace("icm426xx")
ICM426xx = icm426xx_ns.class_("ICM426xx", cg.PollingComponent, i2c.I2CDevice)
ArmWakeAction = icm426xx_ns.class_("ArmWakeAction", automation.Action)


def _g(value):
    """An acceleration: 0.1, "100mg" or "4g"."""
    if isinstance(value, str):
        text = value.strip().lower()
        if text.endswith("mg"):
            return cv.float_(text[:-2]) / 1000
        if text.endswith("g"):
            return cv.float_(text[:-1])
    return cv.float_(value)


CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ICM426xx),
            # Change between two samples (10 ms apart) that counts as movement
            cv.Optional(CONF_MOTION_THRESHOLD, default="100mg"): cv.All(
                _g, cv.Range(min=0.01, max=2.0)
            ),
            # Acceleration beyond gravity that counts as an impact (0: none)
            cv.Optional(CONF_IMPACT_THRESHOLD, default="4g"): cv.All(
                _g, cv.Range(min=0.0, max=15.0)
            ),
            # The motion binary sensor stays on this long after the last movement
            cv.Optional(CONF_MOTION_HOLD, default="10s"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(min=cv.TimePeriod(seconds=1)),
            ),
        }
    )
    .extend(cv.polling_component_schema("30s"))
    .extend(i2c.i2c_device_schema(0x68))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    cg.add(var.set_motion_threshold(config[CONF_MOTION_THRESHOLD]))
    cg.add(var.set_impact_threshold(config[CONF_IMPACT_THRESHOLD]))
    cg.add(var.set_motion_hold(config[CONF_MOTION_HOLD]))


@automation.register_action(
    "icm426xx.arm_wake",
    ArmWakeAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(ICM426xx),
            # 0: no wake on motion, the accelerometer off
            cv.Required(CONF_THRESHOLD): cv.templatable(
                cv.All(_g, cv.Range(min=0.0, max=1.0))
            ),
        }
    ),
    synchronous=True,
)
async def arm_wake_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    threshold = await cg.templatable(config[CONF_THRESHOLD], args, float)
    cg.add(var.set_threshold(threshold))
    return var
