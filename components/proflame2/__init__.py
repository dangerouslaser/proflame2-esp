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
    binary_sensor,
    display,
    font,
    text_sensor,
)
from esphome.const import (
    CONF_ID,
    CONF_CS_PIN,
    CONF_OUTPUT_ID,
    CONF_SENSOR,
    ENTITY_CATEGORY_DIAGNOSTIC,
)
from esphome import pins

DEPENDENCIES = ["spi", "switch", "number", "button", "light", "fan", "climate", "sensor"]
AUTO_LOAD = ["switch", "number", "button", "light", "fan", "climate", "sensor", "binary_sensor", "text_sensor"]

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

# Standalone physical UI (display + encoder) — only meaningful on hardware
# with a screen and rotary encoder, e.g. the LilyGo T-Embed CC1101.
ProFlame2UI = proflame2_ns.class_("ProFlame2UI", cg.Component)

ProFlame2PairButton = proflame2_ns.class_(
    "ProFlame2PairButton", button.Button, cg.Component
)
ProFlame2PairConfirmButton = proflame2_ns.class_(
    "ProFlame2PairConfirmButton", button.Button, cg.Component
)
ProFlame2PairCancelButton = proflame2_ns.class_(
    "ProFlame2PairCancelButton", button.Button, cg.Component
)

CONF_GDO0_PIN = "gdo0_pin"
# T-Embed CC1101 board-quirk pins. Optional everywhere; ignored on plain
# ESP32 + breakout hardware. See proflame2_cc1101.h for semantics.
CONF_PWR_EN_PIN = "pwr_en_pin"
CONF_ANT_SW0_PIN = "ant_sw0_pin"
CONF_ANT_SW1_PIN = "ant_sw1_pin"
CONF_SERIAL_NUMBER = "serial_number"
CONF_POWER = "power"
CONF_PILOT = "pilot"
CONF_AUX = "aux"
CONF_SECONDARY_FLAME = "secondary_flame"
CONF_FLAME = "flame"
CONF_FAN = "fan"
CONF_LIGHT = "light"
CONF_PAIR = "pair"
CONF_PAIR_CONFIRM = "pair_confirm"
CONF_PAIR_CANCEL = "pair_cancel"
CONF_CLIMATE = "climate"
CONF_HYSTERESIS = "hysteresis"
CONF_HEAT_FLAME_LEVEL = "heat_flame_level"
CONF_HEAT_FAN_LEVEL = "heat_fan_level"
CONF_HEAT_SECONDARY_FLAME = "heat_secondary_flame"
CONF_ECC_CONSTANTS = "ecc_constants"
CONF_ECC_C1 = "c1"
CONF_ECC_D1 = "d1"
CONF_ECC_C2 = "c2"
CONF_ECC_D2 = "d2"

# Read-only diagnostic text-sensor exports of the active pairing identity.
# All three are populated once at setup() (after NVS load) and re-published
# whenever learn-mode commits a new candidate.
CONF_SERIAL_NUMBER_SENSOR = "serial_number_sensor"
CONF_ECC_CONSTANTS_SENSOR = "ecc_constants_sensor"
CONF_PAIRING_SOURCE_SENSOR = "pairing_source_sensor"

CONF_UI = "ui"
CONF_DISPLAY = "display"
CONF_ENCODER = "encoder"
CONF_ENCODER_BUTTON = "encoder_button"
CONF_PAIR_BUTTON = "pair_button"
CONF_STATUS_SENSOR = "status_sensor"
CONF_BATTERY_SENSOR = "battery_sensor"
CONF_BACKLIGHT = "backlight"
CONF_LEDS_SWITCH = "leds_switch"
CONF_FONTS = "fonts"
CONF_FONT_SMALL = "small"
CONF_FONT_MEDIUM = "medium"
CONF_FONT_LARGE = "large"

# Defaults match the dangerouslaser pairing (serial 0x320A02). Other
# fireplaces will need different values — see README "Pairing your remote".
ECC_DEFAULTS = {
    CONF_ECC_C1: 0x08,
    CONF_ECC_D1: 0x0E,
    CONF_ECC_C2: 0x0B,
    CONF_ECC_D2: 0x07,
}

ECC_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_ECC_C1, default=ECC_DEFAULTS[CONF_ECC_C1]): cv.hex_int_range(
            min=0, max=0xF
        ),
        cv.Optional(CONF_ECC_D1, default=ECC_DEFAULTS[CONF_ECC_D1]): cv.hex_int_range(
            min=0, max=0xF
        ),
        cv.Optional(CONF_ECC_C2, default=ECC_DEFAULTS[CONF_ECC_C2]): cv.hex_int_range(
            min=0, max=0xF
        ),
        cv.Optional(CONF_ECC_D2, default=ECC_DEFAULTS[CONF_ECC_D2]): cv.hex_int_range(
            min=0, max=0xF
        ),
    }
)

LIGHT_SCHEMA = light.light_schema(ProFlame2Light, light.LightType.BRIGHTNESS_ONLY)

CLIMATE_SCHEMA = climate.climate_schema(ProFlame2Climate).extend(
    {
        cv.Required(CONF_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_HYSTERESIS, default="0.5°C"): cv.temperature,
    }
)

# Optional sub-component: a standalone physical UI driving an LCD + rotary
# encoder. Wired up via existing display / sensor / binary_sensor / font
# components; this schema just glues IDs together. Only meaningful on hardware
# like the LilyGo T-Embed CC1101.
UI_FONTS_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_FONT_SMALL): cv.use_id(font.Font),
        cv.Required(CONF_FONT_MEDIUM): cv.use_id(font.Font),
        cv.Required(CONF_FONT_LARGE): cv.use_id(font.Font),
    }
)

UI_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ProFlame2UI),
        cv.Required(CONF_DISPLAY): cv.use_id(display.Display),
        cv.Required(CONF_ENCODER): cv.use_id(sensor.Sensor),
        cv.Required(CONF_ENCODER_BUTTON): cv.use_id(binary_sensor.BinarySensor),
        cv.Optional(CONF_PAIR_BUTTON): cv.use_id(binary_sensor.BinarySensor),
        cv.Optional(CONF_STATUS_SENSOR): cv.use_id(binary_sensor.BinarySensor),
        cv.Optional(CONF_BATTERY_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_BACKLIGHT): cv.use_id(light.LightState),
        cv.Optional(CONF_LEDS_SWITCH): cv.use_id(switch.Switch),
        cv.Required(CONF_FONTS): UI_FONTS_SCHEMA,
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ProFlame2Component),
        cv.Required(CONF_CS_PIN): pins.gpio_output_pin_schema,
        cv.Optional(CONF_GDO0_PIN): pins.gpio_input_pin_schema,
        cv.Optional(CONF_PWR_EN_PIN): pins.gpio_output_pin_schema,
        cv.Optional(CONF_ANT_SW0_PIN): pins.gpio_output_pin_schema,
        cv.Optional(CONF_ANT_SW1_PIN): pins.gpio_output_pin_schema,
        cv.Optional(CONF_SERIAL_NUMBER, default=0x12345678): cv.hex_uint32_t,
        cv.Optional(CONF_ECC_CONSTANTS, default=ECC_DEFAULTS): ECC_SCHEMA,
        cv.Optional(CONF_POWER): switch.switch_schema(ProFlame2PowerSwitch),
        cv.Optional(CONF_PILOT): switch.switch_schema(ProFlame2PilotSwitch),
        cv.Optional(CONF_AUX): switch.switch_schema(ProFlame2AuxSwitch),
        cv.Optional(CONF_SECONDARY_FLAME): switch.switch_schema(
            ProFlame2SecondaryFlameSwitch
        ),
        cv.Optional(CONF_FLAME): number.number_schema(ProFlame2FlameNumber),
        cv.Optional(CONF_FAN): number.number_schema(ProFlame2FanNumber),
        cv.Optional(CONF_LIGHT): LIGHT_SCHEMA,
        cv.Optional(CONF_PAIR): button.button_schema(ProFlame2PairButton),
        cv.Optional(CONF_PAIR_CONFIRM): button.button_schema(ProFlame2PairConfirmButton),
        cv.Optional(CONF_PAIR_CANCEL): button.button_schema(ProFlame2PairCancelButton),
        cv.Optional(CONF_HEAT_FLAME_LEVEL): number.number_schema(ProFlame2ConfigNumber),
        cv.Optional(CONF_HEAT_FAN_LEVEL): number.number_schema(ProFlame2ConfigNumber),
        cv.Optional(CONF_HEAT_SECONDARY_FLAME): switch.switch_schema(
            ProFlame2HeatSecondaryFlameSwitch
        ),
        cv.Optional(CONF_CLIMATE): CLIMATE_SCHEMA,
        cv.Optional(CONF_UI): UI_SCHEMA,
        cv.Optional(CONF_SERIAL_NUMBER_SENSOR): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:identifier",
        ),
        cv.Optional(CONF_ECC_CONSTANTS_SENSOR): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:key-variant",
        ),
        cv.Optional(CONF_PAIRING_SOURCE_SENSOR): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:database-search",
        ),
    }
).extend(spi.spi_device_schema())


async def _add_switch(parent, conf, parent_setter):
    """Register a proflame2 switch entity, set its parent, and bind it
    to the parent component via the supplied setter (e.g.
    ``var.set_power_switch``)."""
    sw = cg.new_Pvariable(conf[CONF_ID])
    await cg.register_component(sw, conf)
    await switch.register_switch(sw, conf)
    cg.add(sw.set_parent(parent))
    cg.add(parent_setter(sw))
    return sw


async def _add_number(parent, conf, parent_setter, *, min_value, max_value, step, default=None):
    """Register a proflame2 number entity. ``parent_setter`` may be None
    for config-only numbers (e.g. ProFlame2ConfigNumber) that don't have
    a parent back-reference and aren't bound back to the parent."""
    num = cg.new_Pvariable(conf[CONF_ID])
    await cg.register_component(num, conf)
    await number.register_number(
        num, conf, min_value=min_value, max_value=max_value, step=step
    )
    if default is not None:
        cg.add(num.set_default_value(default))
    if parent_setter is not None:
        cg.add(num.set_parent(parent))
        cg.add(parent_setter(num))
    return num


async def _add_button(parent, conf):
    """Register a proflame2 button entity bound only to the parent."""
    btn = cg.new_Pvariable(conf[CONF_ID])
    await cg.register_component(btn, conf)
    await button.register_button(btn, conf)
    cg.add(btn.set_parent(parent))
    return btn


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)
    
    # Set serial number
    cg.add(var.set_serial_number(config[CONF_SERIAL_NUMBER]))

    # ECC pairing constants
    ecc = config[CONF_ECC_CONSTANTS]
    cg.add(
        var.set_ecc_constants(
            ecc[CONF_ECC_C1],
            ecc[CONF_ECC_D1],
            ecc[CONF_ECC_C2],
            ecc[CONF_ECC_D2],
        )
    )
    
    # Configure GDO0 pin if provided
    if CONF_GDO0_PIN in config:
        pin = await cg.gpio_pin_expression(config[CONF_GDO0_PIN])
        cg.add(var.set_gdo0_pin(pin))

    # T-Embed CC1101 board-quirk pins. Driven before SPI starts so the radio
    # rail is powered and the antenna switch is routed for 315 MHz before the
    # first register access.
    if CONF_PWR_EN_PIN in config:
        pin = await cg.gpio_pin_expression(config[CONF_PWR_EN_PIN])
        cg.add(var.set_pwr_en_pin(pin))
    if CONF_ANT_SW0_PIN in config:
        pin = await cg.gpio_pin_expression(config[CONF_ANT_SW0_PIN])
        cg.add(var.set_ant_sw0_pin(pin))
    if CONF_ANT_SW1_PIN in config:
        pin = await cg.gpio_pin_expression(config[CONF_ANT_SW1_PIN])
        cg.add(var.set_ant_sw1_pin(pin))
    
    # Configure power switch
    if CONF_POWER in config:
        await _add_switch(var, config[CONF_POWER], var.set_power_switch)

    # Configure pilot switch
    if CONF_PILOT in config:
        await _add_switch(var, config[CONF_PILOT], var.set_pilot_switch)

    # Configure aux switch
    if CONF_AUX in config:
        await _add_switch(var, config[CONF_AUX], var.set_aux_switch)

    # Configure secondary flame switch
    if CONF_SECONDARY_FLAME in config:
        await _add_switch(
            var, config[CONF_SECONDARY_FLAME], var.set_secondary_flame_switch
        )

    # Configure flame number
    if CONF_FLAME in config:
        await _add_number(
            var, config[CONF_FLAME], var.set_flame_number,
            min_value=0, max_value=6, step=1,
        )

    # Configure fan number
    if CONF_FAN in config:
        await _add_number(
            var, config[CONF_FAN], var.set_fan_number,
            min_value=0, max_value=6, step=1,
        )
    
    # Configure light entity (brightness 0–100% maps to fireplace levels 1–6)
    if CONF_LIGHT in config:
        conf = config[CONF_LIGHT]
        out = cg.new_Pvariable(conf[CONF_OUTPUT_ID])
        await light.register_light(out, conf)
        cg.add(out.set_parent(var))
        light_state = await cg.get_variable(conf[CONF_ID])
        cg.add(var.set_light_state(light_state))

    if CONF_PAIR in config:
        await _add_button(var, config[CONF_PAIR])

    if CONF_PAIR_CONFIRM in config:
        await _add_button(var, config[CONF_PAIR_CONFIRM])

    if CONF_PAIR_CANCEL in config:
        await _add_button(var, config[CONF_PAIR_CANCEL])

    # Diagnostic text sensors — populated by the C++ component at setup() once
    # the NVS-vs-YAML resolution is done, and re-published on a successful
    # learn-mode commit.
    if CONF_SERIAL_NUMBER_SENSOR in config:
        ts = await text_sensor.new_text_sensor(config[CONF_SERIAL_NUMBER_SENSOR])
        cg.add(var.set_serial_number_sensor(ts))
    if CONF_ECC_CONSTANTS_SENSOR in config:
        ts = await text_sensor.new_text_sensor(config[CONF_ECC_CONSTANTS_SENSOR])
        cg.add(var.set_ecc_constants_sensor(ts))
    if CONF_PAIRING_SOURCE_SENSOR in config:
        ts = await text_sensor.new_text_sensor(config[CONF_PAIRING_SOURCE_SENSOR])
        cg.add(var.set_pairing_source_sensor(ts))

    # Heat-mode config entities (consumed by the climate). Registered before
    # the climate so we can hand the references in at climate construction.
    heat_flame_num = None
    if CONF_HEAT_FLAME_LEVEL in config:
        heat_flame_num = await _add_number(
            var, config[CONF_HEAT_FLAME_LEVEL], parent_setter=None,
            min_value=1, max_value=6, step=1, default=6.0,
        )

    heat_fan_num = None
    if CONF_HEAT_FAN_LEVEL in config:
        heat_fan_num = await _add_number(
            var, config[CONF_HEAT_FAN_LEVEL], parent_setter=None,
            min_value=0, max_value=6, step=1, default=0.0,
        )

    # Config-only switch — no parent-back-reference and no parent setter.
    # Stashed for the climate to pick up below.
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

    # Optional standalone UI sub-component (LCD + rotary encoder + button).
    # Wired against pre-declared display / sensor / binary_sensor / font IDs;
    # the actual rendering happens via the display lambda calling
    # id(<ui_id>)->draw(it).
    if CONF_UI in config:
        conf = config[CONF_UI]
        ui = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(ui, conf)
        cg.add(ui.set_parent(var))
        # CONF_DISPLAY is validated at config time (cv.use_id) but not bound to
        # the C++ component — rendering happens via the display lambda calling
        # id(<ui_id>)->draw(it), which already has the display reference.
        enc = await cg.get_variable(conf[CONF_ENCODER])
        cg.add(ui.set_encoder(enc))
        btn = await cg.get_variable(conf[CONF_ENCODER_BUTTON])
        cg.add(ui.set_encoder_button(btn))
        if CONF_PAIR_BUTTON in conf:
            pair_btn = await cg.get_variable(conf[CONF_PAIR_BUTTON])
            cg.add(ui.set_pair_button(pair_btn))
        if CONF_STATUS_SENSOR in conf:
            status = await cg.get_variable(conf[CONF_STATUS_SENSOR])
            cg.add(ui.set_status_sensor(status))
        if CONF_BATTERY_SENSOR in conf:
            battery = await cg.get_variable(conf[CONF_BATTERY_SENSOR])
            cg.add(ui.set_battery_sensor(battery))
        if CONF_BACKLIGHT in conf:
            backlight = await cg.get_variable(conf[CONF_BACKLIGHT])
            cg.add(ui.set_backlight(backlight))
        if CONF_LEDS_SWITCH in conf:
            leds_sw = await cg.get_variable(conf[CONF_LEDS_SWITCH])
            cg.add(ui.set_leds_switch(leds_sw))
        fonts = conf[CONF_FONTS]
        font_small = await cg.get_variable(fonts[CONF_FONT_SMALL])
        font_medium = await cg.get_variable(fonts[CONF_FONT_MEDIUM])
        font_large = await cg.get_variable(fonts[CONF_FONT_LARGE])
        cg.add(ui.set_font_small(font_small))
        cg.add(ui.set_font_medium(font_medium))
        cg.add(ui.set_font_large(font_large))
