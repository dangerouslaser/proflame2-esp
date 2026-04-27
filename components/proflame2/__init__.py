import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import (
    spi,
    switch,
    number,
    button,
    light,
    climate,
    sensor,
)
from esphome.const import (
    CONF_ID,
    CONF_CS_PIN,
    CONF_OUTPUT_ID,
    CONF_SENSOR,
)
from esphome import pins

DEPENDENCIES = ["spi", "switch", "number", "button", "light", "fan", "climate", "sensor"]
AUTO_LOAD = ["switch", "number", "button", "light", "fan", "climate", "sensor"]

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

# Number types (flame + fan; light migrated to a Light entity below)
ProFlame2FlameNumber = proflame2_ns.class_(
    "ProFlame2FlameNumber", number.Number, cg.Component
)
ProFlame2FanNumber = proflame2_ns.class_(
    "ProFlame2FanNumber", number.Number, cg.Component
)

# Persistent config entities consumed by the climate when it auto-activates the
# burner: heat flame level, heat fan level, and whether secondary flame should
# come on while heating. Pure config — no parent callback required.
ProFlame2ConfigNumber = proflame2_ns.class_(
    "ProFlame2ConfigNumber", number.Number, cg.Component
)
ProFlame2HeatSecondaryFlameSwitch = proflame2_ns.class_(
    "ProFlame2HeatSecondaryFlameSwitch", switch.Switch, cg.Component
)

# Light (replaces former ProFlame2LightNumber so HA exposes it as a HomeKit Light)
ProFlame2Light = proflame2_ns.class_("ProFlame2Light", light.LightOutput)

# Climate (HEAT/OFF thermostat over a HA-provided room temperature sensor)
ProFlame2Climate = proflame2_ns.class_(
    "ProFlame2Climate", climate.Climate, cg.Component
)

ProFlame2SendButton = proflame2_ns.class_(
    "ProFlame2SendButton", button.Button, cg.Component
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
CONF_SEND = "send"
CONF_CLIMATE = "climate"
CONF_HYSTERESIS = "hysteresis"
CONF_HEAT_FLAME_LEVEL = "heat_flame_level"
CONF_HEAT_FAN_LEVEL = "heat_fan_level"
CONF_HEAT_SECONDARY_FLAME = "heat_secondary_flame"

LIGHT_SCHEMA = light.light_schema(ProFlame2Light, light.LightType.BRIGHTNESS_ONLY)

CLIMATE_SCHEMA = climate.climate_schema(ProFlame2Climate).extend(
    {
        cv.Required(CONF_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_HYSTERESIS, default="0.5°C"): cv.temperature,
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ProFlame2Component),
        cv.Required(CONF_CS_PIN): pins.gpio_output_pin_schema,
        cv.Optional(CONF_GDO0_PIN): pins.gpio_input_pin_schema,
        cv.Optional(CONF_SERIAL_NUMBER, default=0x12345678): cv.hex_uint32_t,
        cv.Optional(CONF_POWER): switch.switch_schema(ProFlame2PowerSwitch),
        cv.Optional(CONF_PILOT): switch.switch_schema(ProFlame2PilotSwitch),
        cv.Optional(CONF_AUX): switch.switch_schema(ProFlame2AuxSwitch),
        cv.Optional(CONF_SECONDARY_FLAME): switch.switch_schema(
            ProFlame2SecondaryFlameSwitch
        ),
        cv.Optional(CONF_FLAME): number.number_schema(ProFlame2FlameNumber),
        cv.Optional(CONF_FAN): number.number_schema(ProFlame2FanNumber),
        cv.Optional(CONF_LIGHT): LIGHT_SCHEMA,
        cv.Optional(CONF_SEND): button.button_schema(ProFlame2SendButton),
        cv.Optional(CONF_HEAT_FLAME_LEVEL): number.number_schema(ProFlame2ConfigNumber),
        cv.Optional(CONF_HEAT_FAN_LEVEL): number.number_schema(ProFlame2ConfigNumber),
        cv.Optional(CONF_HEAT_SECONDARY_FLAME): switch.switch_schema(
            ProFlame2HeatSecondaryFlameSwitch
        ),
        cv.Optional(CONF_CLIMATE): CLIMATE_SCHEMA,
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
    
    # Configure light entity (brightness 0–100% maps to fireplace levels 1–6)
    if CONF_LIGHT in config:
        conf = config[CONF_LIGHT]
        out = cg.new_Pvariable(conf[CONF_OUTPUT_ID])
        await light.register_light(out, conf)
        cg.add(out.set_parent(var))
        light_state = await cg.get_variable(conf[CONF_ID])
        cg.add(var.set_light_state(light_state))

    if CONF_SEND in config:
        conf = config[CONF_SEND]
        btn = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(btn, conf)
        await button.register_button(btn, conf)
        cg.add(btn.set_parent(var))

    # Heat-mode config entities (consumed by the climate). Registered before
    # the climate so we can hand the references in at climate construction.
    heat_flame_num = None
    if CONF_HEAT_FLAME_LEVEL in config:
        conf = config[CONF_HEAT_FLAME_LEVEL]
        num = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(num, conf)
        await number.register_number(
            num, conf, min_value=1, max_value=6, step=1
        )
        cg.add(num.set_default_value(6.0))
        heat_flame_num = num

    heat_fan_num = None
    if CONF_HEAT_FAN_LEVEL in config:
        conf = config[CONF_HEAT_FAN_LEVEL]
        num = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(num, conf)
        await number.register_number(
            num, conf, min_value=0, max_value=6, step=1
        )
        cg.add(num.set_default_value(0.0))
        heat_fan_num = num

    heat_secondary_sw = None
    if CONF_HEAT_SECONDARY_FLAME in config:
        conf = config[CONF_HEAT_SECONDARY_FLAME]
        sw = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(sw, conf)
        await switch.register_switch(sw, conf)
        heat_secondary_sw = sw

    # Configure climate (thermostat layered on top of the parent)
    if CONF_CLIMATE in config:
        conf = config[CONF_CLIMATE]
        clim = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(clim, conf)
        await climate.register_climate(clim, conf)
        cg.add(clim.set_parent(var))
        sens = await cg.get_variable(conf[CONF_SENSOR])
        cg.add(clim.set_sensor(sens))
        cg.add(clim.set_hysteresis(conf[CONF_HYSTERESIS]))
        if heat_flame_num is not None:
            cg.add(clim.set_heat_flame_level(heat_flame_num))
        if heat_fan_num is not None:
            cg.add(clim.set_heat_fan_level(heat_fan_num))
        if heat_secondary_sw is not None:
            cg.add(clim.set_heat_secondary_flame(heat_secondary_sw))
        cg.add(var.set_climate(clim))
