use crate::physics::{DIMENSIONLESS, LENGTH, PRESSURE, TEMPERATURE, VELOCITY};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
#[serde(untagged)]
pub enum ParameterValue {
    Bool(bool),
    Number(f64),
    Text(String),
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "op", rename_all = "snake_case")]
pub enum VisibleIf {
    Eq { parameter: String, value: ParameterValue },
    Not { term: Box<VisibleIf> },
    All { terms: Vec<VisibleIf> },
    Any { terms: Vec<VisibleIf> },
}

impl VisibleIf {
    pub fn evaluate(&self, values: &BTreeMap<String, ParameterValue>) -> bool {
        match self {
            Self::Eq { parameter, value } => values.get(parameter) == Some(value),
            Self::Not { term } => !term.evaluate(values),
            Self::All { terms } => terms.iter().all(|term| term.evaluate(values)),
            Self::Any { terms } => terms.iter().any(|term| term.evaluate(values)),
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct ParameterSchema {
    pub id: String,
    pub label: String,
    pub dimensions: [i8; 7],
    pub display_unit: String,
    pub default: ParameterValue,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub visible_if: Option<VisibleIf>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct ModuleSchema {
    pub id: String,
    pub label: String,
    pub category: String,
    pub accent: String,
    pub parameters: Vec<ParameterSchema>,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct UiSchema {
    pub version: String,
    pub modules: Vec<ModuleSchema>,
}

impl UiSchema {
    pub fn module(&self, id: &str) -> Option<&ModuleSchema> {
        self.modules.iter().find(|module| module.id == id)
    }

    pub fn visible_parameters<'a>(
        &'a self,
        module: &'a ModuleSchema,
        values: &BTreeMap<String, ParameterValue>,
    ) -> impl Iterator<Item = &'a ParameterSchema> {
        module.parameters.iter().filter(move |parameter| {
            parameter.visible_if.as_ref().map_or(true, |predicate| predicate.evaluate(values))
        })
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct BlockInstance {
    pub id: String,
    pub module_id: String,
    pub enabled: bool,
    pub parameters: BTreeMap<String, ParameterValue>,
}

#[derive(Clone, Debug)]
pub struct ModuleRegistry {
    schema: UiSchema,
}

impl ModuleRegistry {
    pub fn new(schema: UiSchema) -> Self { Self { schema } }

    /// Mirrors `ModuleRegistry.export_ui_schema()` returned by `schema.get`.
    pub fn export_ui_schema(&self) -> Result<serde_json::Value, serde_json::Error> {
        serde_json::to_value(&self.schema)
    }

    pub fn schema(&self) -> &UiSchema { &self.schema }

    pub fn reference_schema() -> Self {
        let equal = |parameter: &str, value: &str| VisibleIf::Eq {
            parameter: parameter.into(), value: ParameterValue::Text(value.into()),
        };
        Self::new(UiSchema {
            version: "3.8".into(),
            modules: vec![
                ModuleSchema {
                    id: "geometry.import".into(), label: "Import geometry".into(), category: "geometry".into(), accent: "#004AFD".into(),
                    parameters: vec![
                        parameter("source", "Source file", DIMENSIONLESS, "path", ParameterValue::Text("naca0018_revC.step".into()), None),
                        parameter("scale", "Model scale", DIMENSIONLESS, "1", ParameterValue::Number(1.0), None),
                    ],
                },
                ModuleSchema {
                    id: "domain.enclosure".into(), label: "Enclosure".into(), category: "domain".into(), accent: "#B9F500".into(),
                    parameters: vec![
                        parameter("length", "Domain length", LENGTH, "m", ParameterValue::Number(10.0), None),
                        parameter("height", "Domain height", LENGTH, "m", ParameterValue::Number(0.8), None),
                        parameter("span", "Domain span", LENGTH, "m", ParameterValue::Number(0.8), None),
                    ],
                },
                ModuleSchema {
                    id: "inlet.velocity".into(), label: "Inlet velocity".into(), category: "boundary".into(), accent: "#FC75B4".into(),
                    parameters: vec![
                        parameter("profile", "Velocity profile", DIMENSIONLESS, "mode", ParameterValue::Text("uniform".into()), None),
                        parameter("magnitude", "Velocity magnitude", VELOCITY, "m/s", ParameterValue::Number(12.5), None),
                        parameter("temperature", "Temperature", TEMPERATURE, "K", ParameterValue::Number(293.15), None),
                        parameter("turbulence_intensity", "Turbulence intensity", DIMENSIONLESS, "1", ParameterValue::Number(0.05), Some(equal("profile", "fully_developed"))),
                    ],
                },
                ModuleSchema {
                    id: "pressure.reference".into(), label: "Pressure reference".into(), category: "physics".into(), accent: "#FEC40B".into(),
                    parameters: vec![
                        parameter("variable", "Pressure variable", DIMENSIONLESS, "p | p_rgh", ParameterValue::Text("p".into()), None),
                        parameter("reference", "Reference pressure", PRESSURE, "Pa", ParameterValue::Number(101_325.0), None),
                        parameter("z_ref", "Reference elevation", LENGTH, "m", ParameterValue::Number(0.0), Some(equal("variable", "p_rgh"))),
                    ],
                },
            ],
        })
    }

    pub fn reference_stack(&self) -> Vec<BlockInstance> {
        self.schema.modules.iter().map(|module| BlockInstance {
            id: format!("block-{}", module.id),
            module_id: module.id.clone(),
            enabled: true,
            parameters: module.parameters.iter().map(|parameter| (parameter.id.clone(), parameter.default.clone())).collect(),
        }).collect()
    }
}

fn parameter(
    id: &str,
    label: &str,
    dimensions: [i8; 7],
    display_unit: &str,
    default: ParameterValue,
    visible_if: Option<VisibleIf>,
) -> ParameterSchema {
    ParameterSchema {
        id: id.into(), label: label.into(), dimensions, display_unit: display_unit.into(), default, visible_if,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn schema_visibility_is_evaluated_from_parameter_values() {
        let registry = ModuleRegistry::reference_schema();
        let module = registry.schema().module("pressure.reference").unwrap();
        let values = module.parameters.iter().map(|parameter| {
            let value = if parameter.id == "variable" { ParameterValue::Text("p_rgh".into()) } else { parameter.default.clone() };
            (parameter.id.clone(), value)
        }).collect::<BTreeMap<_, _>>();
        let visible = registry.schema().visible_parameters(module, &values).map(|parameter| parameter.id.as_str()).collect::<Vec<_>>();
        assert!(visible.contains(&"z_ref"));
    }
}