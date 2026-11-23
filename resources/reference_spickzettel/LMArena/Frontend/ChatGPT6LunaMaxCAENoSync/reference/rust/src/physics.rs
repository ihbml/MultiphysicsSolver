use serde::{Deserialize, Serialize};
use thiserror::Error;

/// SI base order follows FoamField: [kg, m, s, K, mol, A, cd].
pub type Dimensions = [i8; 7];
pub const DIMENSIONLESS: Dimensions = [0, 0, 0, 0, 0, 0, 0];
pub const LENGTH: Dimensions = [0, 1, 0, 0, 0, 0, 0];
pub const VELOCITY: Dimensions = [0, 1, -1, 0, 0, 0, 0];
pub const TEMPERATURE: Dimensions = [0, 0, 0, 1, 0, 0, 0];
pub const PRESSURE: Dimensions = [1, -1, -2, 0, 0, 0, 0];
pub const DENSITY: Dimensions = [1, -3, 0, 0, 0, 0, 0];
pub const KINEMATIC_VISCOSITY: Dimensions = [0, 2, -1, 0, 0, 0, 0];
pub const ANGULAR_VELOCITY: Dimensions = [0, 0, -1, 0, 0, 0, 0];

#[derive(Debug, Error, PartialEq)]
pub enum UnitError {
    #[error("expected a finite number followed by a supported unit")]
    Syntax,
    #[error("unit `{0}` is not supported")]
    UnknownUnit(String),
    #[error("unit `{unit}` has dimensions {actual:?}; expected {expected:?}")]
    DimensionMismatch { unit: String, actual: Dimensions, expected: Dimensions },
    #[error("absolute pressure p must be greater than zero")]
    NonPositiveAbsolutePressure,
    #[error("value must be finite")]
    NonFinite,
}

#[derive(Clone, Copy, Debug, Serialize, Deserialize, PartialEq)]
pub struct UnitValue {
    pub si_value: f64,
    pub dimensions: Dimensions,
}

#[derive(Clone, Copy)]
struct UnitDef {
    dimensions: Dimensions,
    scale: f64,
    offset: f64,
}

impl UnitValue {
    /// Parse the user's value at the UI boundary, before building an RPC payload.
    pub fn parse(input: &str, expected: Dimensions) -> Result<Self, UnitError> {
        let input = input.trim();
        let split_at = input.find(char::is_whitespace).unwrap_or(input.len());
        let (number, suffix) = input.split_at(split_at);
        let value = number.parse::<f64>().map_err(|_| UnitError::Syntax)?;
        if !value.is_finite() {
            return Err(UnitError::NonFinite);
        }
        let suffix = suffix.trim();
        let definition = unit_def(if suffix.is_empty() { canonical_unit(expected) } else { suffix })
            .ok_or_else(|| UnitError::UnknownUnit(suffix.to_owned()))?;
        if definition.dimensions != expected {
            return Err(UnitError::DimensionMismatch {
                unit: suffix.to_owned(), actual: definition.dimensions, expected,
            });
        }
        Ok(Self { si_value: value * definition.scale + definition.offset, dimensions: expected })
    }

    pub fn to_rpc_json(self) -> serde_json::Value {
        serde_json::json!({ "value_si": self.si_value, "dimensions": self.dimensions })
    }
}

fn canonical_unit(dimensions: Dimensions) -> &'static str {
    match dimensions {
        LENGTH => "m",
        VELOCITY => "m/s",
        TEMPERATURE => "K",
        PRESSURE => "Pa",
        DENSITY => "kg/m3",
        KINEMATIC_VISCOSITY => "m2/s",
        ANGULAR_VELOCITY => "rad/s",
        DIMENSIONLESS => "1",
        _ => "",
    }
}

fn unit_def(unit: &str) -> Option<UnitDef> {
    let def = match unit.to_ascii_lowercase().as_str() {
        "m" => UnitDef { dimensions: LENGTH, scale: 1.0, offset: 0.0 },
        "mm" => UnitDef { dimensions: LENGTH, scale: 1e-3, offset: 0.0 },
        "cm" => UnitDef { dimensions: LENGTH, scale: 1e-2, offset: 0.0 },
        "km" => UnitDef { dimensions: LENGTH, scale: 1e3, offset: 0.0 },
        "m/s" => UnitDef { dimensions: VELOCITY, scale: 1.0, offset: 0.0 },
        "km/h" | "kph" => UnitDef { dimensions: VELOCITY, scale: 1.0 / 3.6, offset: 0.0 },
        "k" => UnitDef { dimensions: TEMPERATURE, scale: 1.0, offset: 0.0 },
        "c" | "°c" | "degc" => UnitDef { dimensions: TEMPERATURE, scale: 1.0, offset: 273.15 },
        "pa" => UnitDef { dimensions: PRESSURE, scale: 1.0, offset: 0.0 },
        "kpa" => UnitDef { dimensions: PRESSURE, scale: 1e3, offset: 0.0 },
        "mpa" => UnitDef { dimensions: PRESSURE, scale: 1e6, offset: 0.0 },
        "bar" => UnitDef { dimensions: PRESSURE, scale: 1e5, offset: 0.0 },
        "atm" => UnitDef { dimensions: PRESSURE, scale: 101_325.0, offset: 0.0 },
        "kg/m3" => UnitDef { dimensions: DENSITY, scale: 1.0, offset: 0.0 },
        "m2/s" | "m^2/s" => UnitDef { dimensions: KINEMATIC_VISCOSITY, scale: 1.0, offset: 0.0 },
        "rad/s" => UnitDef { dimensions: ANGULAR_VELOCITY, scale: 1.0, offset: 0.0 },
        "rpm" => UnitDef { dimensions: ANGULAR_VELOCITY, scale: std::f64::consts::TAU / 60.0, offset: 0.0 },
        "1" | "y+" => UnitDef { dimensions: DIMENSIONLESS, scale: 1.0, offset: 0.0 },
        _ => return None,
    };
    Some(def)
}

#[derive(Clone, Copy, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub enum PressureVariable {
    /// Thermodynamic absolute pressure; positivity is enforced at commit time.
    AbsoluteP,
    /// Reduced pressure p_rgh; negative values are valid.
    ReducedPRgh,
}

impl PressureVariable {
    pub fn validate(self, value: UnitValue) -> Result<(), UnitError> {
        if value.dimensions != PRESSURE { return Err(UnitError::DimensionMismatch { unit: "pressure".into(), actual: value.dimensions, expected: PRESSURE }); }
        if self == Self::AbsoluteP && value.si_value <= 0.0 { return Err(UnitError::NonPositiveAbsolutePressure); }
        Ok(())
    }
}

#[derive(Clone, Copy, Debug, Serialize, Deserialize, PartialEq)]
pub enum WallResolution {
    /// Physical first prism-cell height in meters (not a y+ target).
    FirstLayerHeight { delta_y1_m: f64 },
    /// Dimensionless wall-coordinate target. Requires a flow estimate to infer a height.
    TargetYPlus { value: f64 },
}

impl WallResolution {
    pub fn validate(self) -> Result<(), UnitError> {
        match self {
            Self::FirstLayerHeight { delta_y1_m } if !delta_y1_m.is_finite() || delta_y1_m <= 0.0 => Err(UnitError::NonFinite),
            Self::TargetYPlus { value } if !value.is_finite() || value <= 0.0 => Err(UnitError::NonFinite),
            _ => Ok(()),
        }
    }

    /// Estimate y+ from a physical layer height only when wall shear is available.
    pub fn estimated_y_plus(self, friction_velocity_m_s: f64, nu_m2_s: f64) -> Option<f64> {
        match self {
            Self::FirstLayerHeight { delta_y1_m } if friction_velocity_m_s > 0.0 && nu_m2_s > 0.0 => Some(delta_y1_m * friction_velocity_m_s / nu_m2_s),
            _ => None,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn converts_input_to_si_and_keeps_dimensions() {
        assert!((UnitValue::parse("12.5 mm", LENGTH).unwrap().si_value - 0.0125).abs() < 1e-12);
        assert!((UnitValue::parse("850 C", TEMPERATURE).unwrap().si_value - 1123.15).abs() < 1e-10);
        assert_eq!(UnitValue::parse("3.2 bar", PRESSURE).unwrap().si_value, 320_000.0);
    }

    #[test]
    fn pressure_modes_are_not_interchangeable() {
        let negative = UnitValue::parse("-120 Pa", PRESSURE).unwrap();
        assert!(PressureVariable::ReducedPRgh.validate(negative).is_ok());
        assert_eq!(PressureVariable::AbsoluteP.validate(negative), Err(UnitError::NonPositiveAbsolutePressure));
    }
}