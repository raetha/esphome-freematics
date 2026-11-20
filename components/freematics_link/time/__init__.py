"""Time from the co-processor's GNSS: free, unlike SNTP over LTE."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import time as time_
from esphome.const import CONF_ID

from .. import CONF_FREEMATICS_LINK_ID, FreematicsLink, freematics_link_ns

DEPENDENCIES = ["freematics_link"]

FreematicsTime = freematics_link_ns.class_(
    "FreematicsTime", cg.PollingComponent, time_.RealTimeClock
)

CONFIG_SCHEMA = time_.TIME_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(FreematicsTime),
        cv.GenerateID(CONF_FREEMATICS_LINK_ID): cv.use_id(FreematicsLink),
    }
).extend(cv.polling_component_schema("1min"))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await time_.register_time(var, config)
    await cg.register_component(var, config)
    cg.add(var.set_parent(await cg.get_variable(config[CONF_FREEMATICS_LINK_ID])))
