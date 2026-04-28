"""Battery sensor platform for the BQ27220 fuel gauge on the LilyGo T-Embed CC1101.

YAML usage:

    sensor:
      - platform: proflame2
        id: battery_pct
        name: "Battery"
        update_interval: 30s
        # address defaults to 0x55, the BQ27220 default.

The sensor publishes state-of-charge as a percentage (0..100). Wire its id
into `proflame2.ui.battery_sensor` to render it in the on-device status bar.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c, sensor
from esphome.const import (
    DEVICE_CLASS_BATTERY,
    ENTITY_CATEGORY_DIAGNOSTIC,
    ICON_BATTERY,
    STATE_CLASS_MEASUREMENT,
    UNIT_PERCENT,
)

from . import proflame2_ns

DEPENDENCIES = ["i2c"]

ProFlame2Battery = proflame2_ns.class_(
    "ProFlame2Battery", sensor.Sensor, cg.PollingComponent, i2c.I2CDevice
)

CONFIG_SCHEMA = (
    sensor.sensor_schema(
        ProFlame2Battery,
        unit_of_measurement=UNIT_PERCENT,
        icon=ICON_BATTERY,
        accuracy_decimals=0,
        device_class=DEVICE_CLASS_BATTERY,
        state_class=STATE_CLASS_MEASUREMENT,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    )
    .extend(cv.polling_component_schema("30s"))
    .extend(i2c.i2c_device_schema(0x55))
)


async def to_code(config):
    var = await sensor.new_sensor(config)
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
