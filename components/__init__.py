import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import spi, switch, number
from esphome.const import (
    CONF_ID,
    CONF_CS_PIN,
)
from esphome import pins

DEPENDENCIES = ["spi", "switch", "number", "light", "fan"]
AUTO_LOAD = ["switch", "number", "light", "fan"]

proflame2_ns = cg.esphome_ns.namespace("proflame2")
ProFlame2Component = proflame2_ns.class_(
    "ProFlame2Component", cg.Component, spi.SPIDevice
)

# Switch types
ProFlame2PowerSwitch = proflame2_ns.class_(
    "ProFlame2PowerSwitch", switch.Switch, cg.Component
)
ProFlame2PilotSwitch = proflame2_ns.class_(
    "ProFlame2PilotSwitch", switch.Switch, cg.Component
)
ProFlame2AuxSwitch = proflame2_ns.class_(
    "ProFlame2AuxSwitch", switch.Switch, cg.Component
)
ProFlame2SecondaryFlameSwitch = proflame2_ns.class_(
    "ProFlame2SecondaryFlameSwitch", switch.Switch, cg.Component
)

# Number types
ProFlame2FlameNumber = proflame2_ns.class_(
    "ProFlame2FlameNumber", number.Number, cg.Component
)
ProFlame2FanNumber = proflame2_ns.class_(
    "ProFlame2FanNumber", number.Number, cg.Component
)
ProFlame2LightNumber = proflame2_ns.class_(
    "ProFlame2LightNumber", number.Number, cg.Component
)

CONF_GDO0_PIN = "gdo0_pin"
CONF_SERIAL_NUMBER = "serial_number"
CONF_POWER = "power"
CONF_PILOT = "pilot"
CONF_AUX = "aux"
CONF_SECONDARY_FLAME = "secondary_flame"
CONF_FLAME = "flame"
CONF_FAN = "fan"
CONF_LIGHT = "light"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ProFlame2Component),
        cv.Required(CONF_CS_PIN): pins.gpio_output_pin_schema,
        cv.Optional(CONF_GDO0_PIN): pins.gpio_input_pin_schema,
        cv.Optional(CONF_SERIAL_NUMBER, default=0x12345678): cv.hex_uint32_t,
        cv.Optional(CONF_POWER): switch.switch_schema(ProFlame2PowerSwitch),
        cv.Optional(CONF_PILOT): switch.switch_schema(ProFlame2PilotSwitch),
        cv.Optional(CONF_AUX): switch.switch_schema(ProFlame2AuxSwitch),
        cv.Optional(CONF_SECONDARY_FLAME): switch.switch_schema(ProFlame2SecondaryFlameSwitch),
        cv.Optional(CONF_FLAME): number.number_schema(ProFlame2FlameNumber),
        cv.Optional(CONF_FAN): number.number_schema(ProFlame2FanNumber),
        cv.Optional(CONF_LIGHT): number.number_schema(ProFlame2LightNumber),
    }
).extend(spi.spi_device_schema())


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)
    
    # Set serial number
    cg.add(var.set_serial_number(config[CONF_SERIAL_NUMBER]))
    
    # Configure GDO0 pin if provided
    if CONF_GDO0_PIN in config:
        pin = await cg.gpio_pin_expression(config[CONF_GDO0_PIN])
        cg.add(var.set_gdo0_pin(pin))
    
    # Configure power switch
    if CONF_POWER in config:
        conf = config[CONF_POWER]
        sw = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(sw, conf)
        await switch.register_switch(sw, conf)
        cg.add(sw.set_parent(var))
        cg.add(var.set_power_switch(sw))
    
    # Configure pilot switch
    if CONF_PILOT in config:
        conf = config[CONF_PILOT]
        sw = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(sw, conf)
        await switch.register_switch(sw, conf)
        cg.add(sw.set_parent(var))
        cg.add(var.set_pilot_switch(sw))
    
    # Configure aux switch
    if CONF_AUX in config:
        conf = config[CONF_AUX]
        sw = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(sw, conf)
        await switch.register_switch(sw, conf)
        cg.add(sw.set_parent(var))
        cg.add(var.set_aux_switch(sw))
    
    # Configure secondary flame switch
    if CONF_SECONDARY_FLAME in config:
        conf = config[CONF_SECONDARY_FLAME]
        sw = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(sw, conf)
        await switch.register_switch(sw, conf)
        cg.add(sw.set_parent(var))
        cg.add(var.set_secondary_flame_switch(sw))

    # Configure flame number
    if CONF_FLAME in config:
        conf = config[CONF_FLAME]
        num = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(num, conf)
        await number.register_number(
            num,
            conf,
            min_value=0,
            max_value=6,
            step=1,
        )
        cg.add(num.set_parent(var))
        cg.add(var.set_flame_number(num))
    
    # Configure fan number
    if CONF_FAN in config:
        conf = config[CONF_FAN]
        num = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(num, conf)
        await number.register_number(
            num,
            conf,
            min_value=0,
            max_value=6,
            step=1,
        )
        cg.add(num.set_parent(var))
        cg.add(var.set_fan_number(num))
    
    # Configure light number
    if CONF_LIGHT in config:
        conf = config[CONF_LIGHT]
        num = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(num, conf)
        await number.register_number(
            num,
            conf,
            min_value=0,
            max_value=6,
            step=1,
        )
        cg.add(num.set_parent(var))
        cg.add(var.set_light_number(num))
