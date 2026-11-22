"""The Freematics ONE+ co-processor link: internal GNSS and battery voltage, read only.

The co-processor (an STM32) owns the vehicle bus and the internal GNSS receiver.
This component sends it only commands it answers itself (ATI, ATRV, ATGPSON,
ATGPS, and ATGPSOFF and ATLP before sleeping); there is no way to send it
anything else, so nothing reaches the bus.
"""

import subprocess
from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation, pins
from esphome.components import uart
from esphome.const import CONF_ID, CONF_RESET_PIN

DOMAIN = "freematics_link"
CODEOWNERS = ["@raetha"]
DEPENDENCIES = ["uart"]
MULTI_CONF = False

CONF_FREEMATICS_LINK_ID = "freematics_link_id"
CONF_GNSS_INTERVAL = "gnss_interval"
CONF_BATTERY_INTERVAL = "battery_interval"
CONF_GNSS_RESTART = "gnss_restart"
CONF_ON_FIX = "on_fix"

freematics_link_ns = cg.esphome_ns.namespace("freematics_link")
FreematicsLink = freematics_link_ns.class_(
    "FreematicsLink", cg.Component, uart.UARTDevice
)
Fix = cg.global_ns.namespace("freematics").struct("Fix")


def _interval_or_off(value):
    if value in (0, "0", "0s", "off", False):
        return cv.TimePeriod(milliseconds=0)
    return cv.All(
        cv.positive_time_period_milliseconds,
        cv.Range(min=cv.TimePeriod(milliseconds=500)),
    )(value)


CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(FreematicsLink),
            cv.Optional(CONF_RESET_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_GNSS_INTERVAL, default="2s"): _interval_or_off,
            cv.Optional(CONF_BATTERY_INTERVAL, default="60s"): _interval_or_off,
            # No fix this long: the GNSS is switched off and on again (0s: never)
            cv.Optional(CONF_GNSS_RESTART, default="10min"): _interval_or_off,
            cv.Optional(CONF_ON_FIX): automation.validate_automation({}),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    DOMAIN, baud_rate=115200, require_tx=True, require_rx=True
)


def _source() -> str:
    """The commit of this repository the firmware was built from, as
    "<hash> (<commit date>)", or "unknown": ESPHome fetches components and
    packages with git, so their checkout knows it."""
    here = Path(__file__).resolve().parent
    try:
        out = subprocess.run(
            [
                "git",
                "-C",
                str(here),
                "log",
                "-1",
                "--format=%h (%cd)",
                "--date=format:%Y-%m-%d %H:%M",
            ],
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
        )
    except (OSError, subprocess.SubprocessError):
        return "unknown"
    return f"{out} with local changes" if dirty.returncode == 1 else out or "unknown"


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    cg.add(var.set_source(_source()))
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    if reset := config.get(CONF_RESET_PIN):
        cg.add(var.set_reset_pin(await cg.gpio_pin_expression(reset)))
    cg.add(var.set_gnss_interval(config[CONF_GNSS_INTERVAL].total_milliseconds))
    cg.add(var.set_gnss_restart(config[CONF_GNSS_RESTART].total_milliseconds))
    cg.add(var.set_battery_interval(config[CONF_BATTERY_INTERVAL].total_milliseconds))
    for conf in config.get(CONF_ON_FIX, []):
        await automation.build_automation(var.get_fix_trigger(), [(Fix, "fix")], conf)
