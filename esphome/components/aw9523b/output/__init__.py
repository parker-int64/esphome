import esphome.codegen as cg
from esphome.components import output
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_PIN

from .. import AW9523B_OUTPUT_PIN_SCHEMA, BASE_SCHEMA, CONF_AW9523B_ID, aw9523b_ns

DEPENDENCIES = ["aw9523b"]

AW9523BFloatOutput = aw9523b_ns.class_(
    "AW9523BFloatOutput", output.FloatOutput, cg.Component
)

CONFIG_SCHEMA = output.FLOAT_OUTPUT_SCHEMA.extend(
    {
        cv.Required(CONF_ID): cv.declare_id(AW9523BFloatOutput),
        cv.Required(CONF_PIN): AW9523B_OUTPUT_PIN_SCHEMA,
    }
).extend(BASE_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_parented(var, config[CONF_AW9523B_ID])
    await output.register_output(var, config)
    await cg.register_component(var, config)
    cg.add(var.set_pin(config[CONF_PIN]))
