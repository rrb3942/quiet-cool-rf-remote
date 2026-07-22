import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import fan
from esphome.const import CONF_CS_PIN, CONF_OUTPUT_ID
from esphome.core import CORE
from .. import quiet_cool_ns

# Additional pin configuration keys
CONF_GDO0_PIN = "gdo0_pin"
CONF_GDO2_PIN = "gdo2_pin"
CONF_CLK_PIN = "clk_pin"
CONF_MISO_PIN = "miso_pin"
CONF_MOSI_PIN = "mosi_pin"
CONF_REMOTE_ID = "remote_id"
CONF_FREQ_MHZ = "center_freq_mhz"
CONF_DEVIATION_KHZ = "deviation_khz"

# No `spi` dependency: the vendored CC1101 driver does its own software SPI on
# the pins given below, so there is no ESPHome SPI bus to attach to. Configs
# carrying a top-level `spi:` block should drop it.

QuietCoolFan = quiet_cool_ns.class_("QuietCoolFan", cg.Component, fan.Fan)

CONFIG_SCHEMA = fan.fan_schema(QuietCoolFan).extend(
    {
        cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(QuietCoolFan),
        cv.Required(CONF_CS_PIN                        ): cv.uint8_t,
        cv.Required(CONF_GDO0_PIN                      ): cv.uint8_t,
        cv.Required(CONF_GDO2_PIN                      ): cv.uint8_t,
        cv.Required(CONF_CLK_PIN                       ): cv.uint8_t,
        cv.Required(CONF_MISO_PIN                      ): cv.uint8_t,
        cv.Required(CONF_MOSI_PIN                      ): cv.uint8_t,
        cv.Required(CONF_REMOTE_ID                     ): cv.ensure_list(cv.hex_uint8_t),
        cv.Optional(CONF_FREQ_MHZ     , default=433.897): cv.float_,
        cv.Optional(CONF_DEVIATION_KHZ, default=10.0   ): cv.float_
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])  # type: QuietCoolFan
    await cg.register_component(var, config)
    await fan.register_fan(var, config)

    # On Arduino the vendored CC1101 driver includes <SPI.h>, which used to arrive
    # via the `spi` component we no longer depend on. On esp-idf it uses
    # arduino_compat.h instead and needs no library.
    if CORE.using_arduino:
        cg.add_library("SPI", None)

    cg.add(var.set_pins(config[CONF_CS_PIN], config[CONF_GDO0_PIN], config[CONF_GDO2_PIN],
                        config[CONF_CLK_PIN], config[CONF_MISO_PIN], config[CONF_MOSI_PIN]))
    cg.add(var.set_remote_id(config[CONF_REMOTE_ID]))
    cg.add(var.set_frequencies(config[CONF_FREQ_MHZ], config[CONF_DEVIATION_KHZ]))
