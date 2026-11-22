"""The protocols tracker_uplink can send in: each a schema for its own
settings, a check, and the code that makes its Transport (transport.h).

Adding one: a Transport class in C++ with its codec in a plain header, and an
entry in PROTOCOLS.
"""

from collections.abc import Awaitable, Callable
from dataclasses import dataclass

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import esp32, socket
from esphome.const import CONF_PORT, CONF_URL
from esphome.core import CORE

tracker_uplink_ns = cg.esphome_ns.namespace("tracker_uplink")
Transport = tracker_uplink_ns.class_("Transport")
TeltonikaTransport = tracker_uplink_ns.class_("TeltonikaTransport", Transport)
FreematicsTransport = tracker_uplink_ns.class_("FreematicsTransport", Transport)
WebhookTransport = tracker_uplink_ns.class_("WebhookTransport", Transport)

TRANSPORT_ID = "transport_id"
CONF_HOST = "host"
CONF_DEVICE_ID = "device_id"
CONF_ACK_TIMEOUT = "ack_timeout"


@dataclass(frozen=True)
class Protocol:
    schema: dict
    validate: Callable[[dict], dict]
    to_code: Callable[[dict], Awaitable[cg.MockObj]]


def _device_id(value):
    value = cv.string_strict(value)
    if not 1 <= len(value) <= 32 or not value.isprintable() or not value.isascii():
        raise cv.Invalid("device_id must be 1 to 32 printable ASCII characters")
    return value


def _url(value):
    value = cv.string_strict(value)
    if not value.startswith("https://"):
        raise cv.Invalid("the webhook URL must start with https://")
    return value


def _udp_schema(transport_class, port: int) -> dict:
    return {
        cv.GenerateID(TRANSPORT_ID): cv.declare_id(transport_class),
        cv.Required(CONF_HOST): cv.string_strict,
        cv.Optional(CONF_PORT, default=port): cv.port,
        cv.Required(CONF_DEVICE_ID): _device_id,
    }


def _udp_validate(config):
    socket.consume_sockets(1, "tracker_uplink", socket.SocketType.UDP)(config)
    return config


async def _udp_transport(config):
    socket.require_ipv4_resolve()  # compiles the async DNS lookup it uses
    cg.add_define("USE_UPLINK_UDP")
    var = cg.new_Pvariable(config[TRANSPORT_ID])
    cg.add(var.set_host(config[CONF_HOST]))
    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_device_id(config[CONF_DEVICE_ID]))
    return var


async def _teltonika(config):
    var = await _udp_transport(config)
    cg.add(var.set_ack_timeout(config[CONF_ACK_TIMEOUT]))
    return var


def _webhook_validate(config):
    if not CORE.is_esp32:
        raise cv.Invalid("the home_assistant protocol needs an ESP32")
    socket.consume_sockets(1, "tracker_uplink", socket.SocketType.TCP)(config)
    return config


async def _webhook(config):
    cg.add_define("USE_UPLINK_WEBHOOK")
    esp32.request_http_client()
    esp32.require_certificate_bundle()
    # A TLS session ticket lets the next batch skip most of the handshake
    # when the server has closed the connection in between.
    esp32.require_mbedtls_tls_extras(["CONFIG_MBEDTLS_CLIENT_SSL_SESSION_TICKETS"])
    esp32.add_idf_sdkconfig_option("CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS", True)
    var = cg.new_Pvariable(config[TRANSPORT_ID])
    cg.add(var.set_url(config[CONF_URL]))
    return var


PROTOCOLS: dict[str, Protocol] = {
    # Teltonika GH3000 to Traccar: the densest format it reads, acknowledged
    "teltonika": Protocol(
        schema={
            **_udp_schema(TeltonikaTransport, 5027),
            cv.Optional(CONF_ACK_TIMEOUT, default="15s"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(min=cv.TimePeriod(seconds=2)),
            ),
        },
        validate=_udp_validate,
        to_code=_teltonika,
    ),
    # Traccar's Freematics text protocol: unacknowledged, about 4 times the data
    "freematics": Protocol(
        schema=_udp_schema(FreematicsTransport, 5170),
        validate=_udp_validate,
        to_code=_udp_transport,
    ),
    # Home Assistant's Freematics integration: compact batches to a webhook
    "home_assistant": Protocol(
        schema={
            cv.GenerateID(TRANSPORT_ID): cv.declare_id(WebhookTransport),
            cv.Required(CONF_URL): _url,
        },
        validate=_webhook_validate,
        to_code=_webhook,
    ),
}
