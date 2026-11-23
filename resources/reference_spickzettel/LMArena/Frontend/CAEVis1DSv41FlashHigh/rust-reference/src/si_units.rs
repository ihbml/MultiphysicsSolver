//! SI dimension propagation for derived field expressions.
//!
//! A full expression is parsed and type checked **before** Rhai execution or
//! any CFDFLD03 buffer allocation. Every AST node carries a 7-element exponent
//! vector `[kg, m, s, K, mol, A, cd]`; addition requires identical vectors,
//! multiplication adds exponents, `grad` subtracts one metre, `sqrt` halves.

use std::fmt;
use std::collections::HashMap;
use thiserror::Error;

pub const BASE_UNITS: [&str; 7] = ["kg", "m", "s", "K", "mol", "A", "cd"];

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Dim(pub [f32; 7]);

impl Dim {
    pub const ZERO: Dim = Dim([0.0; 7]);
    pub fn is_zero(&self) -> bool {
        self.0.iter().all(|e| e.abs() < 1.0e-9)
    }
    pub fn add(&self, rhs: &Dim) -> Dim {
        let mut d = [0.0; 7];
        for i in 0..7 {
            d[i] = self.0[i] + rhs.0[i];
        }
        Dim(d)
    }
    pub fn sub(&self, rhs: &Dim) -> Dim {
        let mut d = [0.0; 7];
        for i in 0..7 {
            d[i] = self.0[i] - rhs.0[i];
        }
        Dim(d)
    }
    pub fn scale(&self, s: f32) -> Dim {
        let mut d = [0.0; 7];
        for i in 0..7 {
            d[i] = self.0[i] * s;
        }
        Dim(d)
    }
    pub fn superscript(&self) -> String {
        let sup = |v: f32| -> String {
            if (v.round() - v).abs() < 1.0e-6 {
                script_number(v.round() as i32)
            } else {
                format!("{v:.1}").replace('-', "⁻").replace('.', "·")
            }
        };
        let mut parts = Vec::new();
        for (i, e) in self.0.iter().enumerate() {
            if e.abs() < 1.0e-9 {
                continue;
            }
            let ex = if (*e - 1.0).abs() < 1.0e-9 {
                String::new()
            } else {
                sup(*e)
            };
            parts.push(format!("{}{}", BASE_UNITS[i], ex));
        }
        if parts.is_empty() {
            "1".into()
        } else {
            parts.join("·")
        }
    }
}

fn script_number(n: i32) -> String {
    const SUPER: [&str; 10] = ["⁰", "¹", "²", "³", "⁴", "⁵", "⁶", "⁷", "⁸", "⁹"];
    let mut out = String::new();
    let mut v = n;
    if v < 0 {
        out.push('⁻');
        v = -v;
    }
    for ch in v.to_string().chars() {
        out.push_str(SUPER[ch.to_digit(10).unwrap_or(0) as usize]);
    }
    out
}

impl fmt::Display for Dim {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "[{}]", self.superscript())
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Shape {
    Scalar,
    Vector,
    Tensor,
    Any,
}

impl fmt::Display for Shape {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let s = match self {
            Shape::Scalar => "scalar",
            Shape::Vector => "vector",
            Shape::Tensor => "tensor",
            Shape::Any => "numeric constant",
        };
        write!(f, "{s}")
    }
}

#[derive(Clone, Copy, Debug)]
pub struct Typed {
    pub dim: Dim,
    pub shape: Shape,
    /// Numeric literals are dimension-polymorphic scale factors.
    pub literal: bool,
}

impl Typed {
    pub fn scalar(dim: Dim) -> Self {
        Self { dim, shape: Shape::Scalar, literal: false }
    }
    pub fn vector(dim: Dim) -> Self {
        Self { dim, shape: Shape::Vector, literal: false }
    }
    pub fn literal() -> Self {
        Self { dim: Dim::ZERO, shape: Shape::Any, literal: true }
    }
    pub fn quantity(&self) -> &'static str {
        if self.literal {
            "Numeric constant"
        } else {
            quantity_name(&self.dim)
        }
    }
}

pub fn quantity_name(dim: &Dim) -> &'static str {
    const TABLE: [(Dim, &str); 12] = [
        (Dim([1., -1., -2., 0., 0., 0., 0.]), "Pressure"),
        (Dim([1., -3., 0., 0., 0., 0., 0.]), "Density"),
        (Dim([0., 1., -1., 0., 0., 0., 0.]), "Velocity"),
        (Dim([0., 0., -1., 0., 0., 0., 0.]), "Rate"),
        (Dim([1., -1., -1., 0., 0., 0., 0.]), "Dynamic viscosity"),
        (Dim([0., 2., -2., 0., 0., 0., 0.]), "Specific kinetic energy"),
        (Dim([1., 2., -3., 0., 0., 0., 0.]), "Power"),
        (Dim([1., 1., -3., -1., 0., 0., 0.]), "Thermal conductivity"),
        (Dim([1., -1., -3., -1., 0., 0., 0.]), "Volumetric entropy generation"),
        (Dim([0., 1., 0., 0., 0., 0., 0.]), "Length"),
        (Dim([0., 0., 0., 1., 0., 0., 0.]), "Temperature"),
        (Dim::ZERO, "Dimensionless"),
    ];
    TABLE
        .iter()
        .find(|(d, _)| d.is_close(dim))
        .map(|(_, name)| *name)
        .unwrap_or("Derived quantity")
}

impl Dim {
    fn is_close(&self, other: &Dim) -> bool {
        self.0.iter().zip(other.0.iter()).all(|(a, b)| (a - b).abs() < 1.0e-9)
    }
}

const P: Dim = Dim([1., -1., -2., 0., 0., 0., 0.]);
const KE: Dim = Dim([0., 2., -2., 0., 0., 0., 0.]);
const U: Dim = Dim([0., 1., -1., 0., 0., 0., 0.]);
const RHO: Dim = Dim([1., -3., 0., 0., 0., 0., 0.]);
const MU: Dim = Dim([1., -1., -1., 0., 0., 0., 0.]);
const KAPPA: Dim = Dim([1., 1., -3., -1., 0., 0., 0.]);
const TEMP: Dim = Dim([0., 0., 0., 1., 0., 0., 0.]);
const LENGTH: Dim = Dim([0., 1., 0., 0., 0., 0., 0.]);
const R_GAS: Dim = Dim([0., 2., -2., -1., 0., 0., 0.]);

#[derive(Clone, Debug)]
pub struct Symbol {
    pub typed: Typed,
    pub unit: &'static str,
    pub label: &'static str,
}

#[derive(Clone, Debug)]
pub struct UnitRegistry {
    pub symbols: HashMap<String, Symbol>,
}

impl Default for UnitRegistry {
    fn default() -> Self {
        let mut symbols = HashMap::new();
        let mut add = |name: &str, typed: Typed, unit: &'static str, label: &'static str| {
            symbols.insert(name.to_string(), Symbol { typed, unit, label });
        };
        add("p", Typed::scalar(P), "Pa", "Static pressure");
        add("rho", Typed::scalar(RHO), "kg·m⁻³", "Density");
        add("U", Typed::vector(U), "m·s⁻¹", "Velocity vector");
        add("T", Typed::scalar(TEMP), "K", "Static temperature");
        add("mu", Typed::scalar(MU), "Pa·s", "Dynamic viscosity");
        add("kappa", Typed::scalar(KAPPA), "W·m⁻¹·K⁻¹", "Thermal conductivity");
        add("R_air", Typed::scalar(R_GAS), "J·kg⁻¹·K⁻¹", "Dry-air gas constant");
        add("gamma", Typed::scalar(Dim::ZERO), "—", "Ratio of specific heats");
        add("yPlus", Typed::scalar(Dim::ZERO), "—", "Wall y+");
        add("Nu", Typed::scalar(Dim::ZERO), "—", "Nusselt number");
        add("x", Typed::scalar(LENGTH), "m", "Axial coordinate");
        add("wall_distance", Typed::scalar(LENGTH), "m", "Wall distance");
        Self { symbols }
    }
}

#[derive(Error, Debug, Clone)]
pub enum UnitError {
    #[error("Cannot {op} {left} and {right} — {hint}")]
    Mismatch {
        op: &'static str,
        left: String,
        right: String,
        hint: String,
    },
    #[error("Unknown field “{0}”")]
    Unknown(String),
    #[error("Vector magnitude required: {0}")]
    Shape(String),
    #[error("Parse error: {0}")]
    Parse(String),
    #[error("Function {0}() is not registered")]
    Function(String),
    #[error("Result is a {shape} field, not a scalar — wrap it in mag() or dot()")]
    NonScalarResult { shape: Shape },
}

fn describe(t: &Typed) -> String {
    format!("{} {}", t.quantity(), t.dim)
}

/* ------------------------------- lexer -------------------------------- */
#[derive(Clone, Debug, PartialEq)]
enum Token {
    Number(f64),
    Ident(String),
    Op(char),
}

fn lex(source: &str) -> Result<Vec<Token>, UnitError> {
    let chars: Vec<char> = source.chars().collect();
    let mut out = Vec::new();
    let mut i = 0;
    while i < chars.len() {
        let c = chars[i];
        if c.is_whitespace() {
            i += 1;
            continue;
        }
        if c.is_ascii_digit() || c == '.' {
            let start = i;
            while i < chars.len() && (chars[i].is_ascii_digit() || chars[i] == '.') {
                i += 1;
            }
            if i < chars.len() && (chars[i] == 'e' || chars[i] == 'E') {
                i += 1;
                if i < chars.len() && (chars[i] == '-' || chars[i] == '+') {
                    i += 1;
                }
                while i < chars.len() && chars[i].is_ascii_digit() {
                    i += 1;
                }
            }
            let raw: String = chars[start..i].iter().collect();
            let v = raw
                .parse::<f64>()
                .map_err(|_| UnitError::Parse(format!("invalid number “{raw}”")))?;
            out.push(Token::Number(v));
            continue;
        }
        if c.is_alphabetic() || c == '_' {
            let start = i;
            while i < chars.len() && (chars[i].is_alphanumeric() || chars[i] == '_') {
                i += 1;
            }
            out.push(Token::Ident(chars[start..i].iter().collect()));
            continue;
        }
        if "+-*/^(),".contains(c) {
            out.push(Token::Op(c));
            i += 1;
            continue;
        }
        return Err(UnitError::Parse(format!("unexpected character “{c}”")));
    }
    Ok(out)
}

/* -------------------------------- AST --------------------------------- */
#[derive(Clone, Debug)]
pub enum Ast {
    Number(f64),
    Var(String),
    Neg(Box<Ast>),
    Bin { op: char, a: Box<Ast>, b: Box<Ast> },
    Call { name: String, args: Vec<Ast> },
}

pub struct Parser {
    tokens: Vec<Token>,
    pos: usize,
}

impl Parser {
    pub fn new(source: &str) -> Result<Self, UnitError> {
        Ok(Self { tokens: lex(source)?, pos: 0 })
    }
    fn peek(&self) -> Option<&Token> {
        self.tokens.get(self.pos)
    }
    fn op(&mut self, want: char) -> Option<char> {
        match self.peek() {
            Some(Token::Op(c)) if *c == want => {
                self.pos += 1;
                Some(want)
            }
            _ => None,
        }
    }
    fn parse(mut self) -> Result<Ast, UnitError> {
        let ast = self.expr()?;
        if self.pos != self.tokens.len() {
            return Err(UnitError::Parse(format!("unexpected token at {}", self.pos)));
        }
        Ok(ast)
    }
    fn expr(&mut self) -> Result<Ast, UnitError> {
        let mut a = self.term()?;
        while let Some(c @ ('+' | '-')) = self.peek().and_then(|t| if let Token::Op(c) = t { Some(*c) } else { None }) {
            self.pos += 1;
            let b = self.term()?;
            a = Ast::Bin { op: c, a: Box::new(a), b: Box::new(b) };
        }
        Ok(a)
    }
    fn term(&mut self) -> Result<Ast, UnitError> {
        let mut a = self.power()?;
        while let Some(c @ ('*' | '/')) = self.peek().and_then(|t| if let Token::Op(c) = t { Some(*c) } else { None }) {
            self.pos += 1;
            let b = self.power()?;
            a = Ast::Bin { op: c, a: Box::new(a), b: Box::new(b) };
        }
        Ok(a)
    }
    fn power(&mut self) -> Result<Ast, UnitError> {
        let mut a = self.unary()?;
        while self.op('^').is_some() {
            let b = self.unary()?;
            a = Ast::Bin { op: '^', a: Box::new(a), b: Box::new(b) };
        }
        Ok(a)
    }
    fn unary(&mut self) -> Result<Ast, UnitError> {
        if self.op('-').is_some() {
            return Ok(Ast::Neg(Box::new(self.unary()?)));
        }
        if self.op('+').is_some() {
            return self.unary();
        }
        self.atom()
    }
    fn atom(&mut self) -> Result<Ast, UnitError> {
        let token = self.peek().cloned().ok_or_else(|| UnitError::Parse("unexpected end of expression".into()))?;
        match token {
            Token::Number(v) => {
                self.pos += 1;
                Ok(Ast::Number(v))
            }
            Token::Ident(name) => {
                self.pos += 1;
                if self.op('(').is_some() {
                    let mut args = Vec::new();
                    if self.peek() != Some(&Token::Op(')')) {
                        args.push(self.expr()?);
                        while self.op(',').is_some() {
                            args.push(self.expr()?);
                        }
                    }
                    match self.peek() {
                        Some(Token::Op(')')) => self.pos += 1,
                        _ => return Err(UnitError::Parse(format!("missing ) after {name}("))),
                    }
                    Ok(Ast::Call { name, args })
                } else {
                    Ok(Ast::Var(name))
                }
            }
            Token::Op('(') => {
                self.pos += 1;
                let a = self.expr()?;
                if self.op(')').is_none() {
                    return Err(UnitError::Parse("missing )".into()));
                }
                Ok(a)
            }
            other => Err(UnitError::Parse(format!("unexpected token {other:?}"))),
        }
    }
}

/* ----------------------------- type check ----------------------------- */
#[derive(Clone, Debug)]
pub struct TraceEntry {
    pub node: String,
    pub dims: String,
    pub quantity: &'static str,
    pub shape: Shape,
}

impl UnitRegistry {
    pub fn check(&self, ast: &Ast, trace: &mut Vec<TraceEntry>) -> Result<Typed, UnitError> {
        let typed = self.walk(ast)?;
        trace.push(TraceEntry {
            node: format_ast(ast),
            dims: typed.dim.superscript(),
            quantity: typed.quantity(),
            shape: typed.shape,
        });
        Ok(typed)
    }

    fn walk(&self, ast: &Ast) -> Result<Typed, UnitError> {
        match ast {
            Ast::Number(v) => {
                // Physical constants can be inferred when they are unambiguous;
                // this keeps `sqrt(1.4 * 287.05 * T)` dimensionally meaningful.
                if (*v - 287.05).abs() < 1.0e-6 {
                    Ok(Typed::scalar(R_GAS))
                } else {
                    Ok(Typed::literal())
                }
            }
            Ast::Var(name) => self
                .symbols
                .get(name)
                .map(|s| s.typed)
                .ok_or_else(|| UnitError::Unknown(name.clone())),
            Ast::Neg(a) => self.walk(a),
            Ast::Bin { op, a, b } => {
                let left = self.walk(a)?;
                let right = self.walk(b)?;
                match op {
                    '+' | '-' => {
                        if !left.literal && !right.literal && !left.dim.is_close(&right.dim) {
                            let hint = match (left.dim.is_close(&P), right.dim.is_close(&KE)) {
                                (true, true) => "multiply by Density rho [kg·m⁻³]".to_string(),
                                _ => "make both operands dimensionally identical".to_string(),
                            };
                            return Err(UnitError::Mismatch {
                                op: if *op == '+' { "add" } else { "subtract" },
                                left: describe(&left),
                                right: describe(&right),
                                hint,
                            });
                        }
                        Ok(if left.literal { right } else { left })
                    }
                    '*' => {
                        if left.shape == Shape::Vector && right.shape == Shape::Vector {
                            return Err(UnitError::Shape(
                                "ambiguous vector multiplication; use dot(a, b)".into(),
                            ));
                        }
                        let dim = if left.literal && right.literal {
                            Dim::ZERO
                        } else if left.literal {
                            right.dim
                        } else if right.literal {
                            left.dim
                        } else {
                            left.dim.add(&right.dim)
                        };
                        let shape = match (left.shape, right.shape) {
                            (Shape::Any, other) | (other, Shape::Any) => other,
                            (Shape::Vector, _) | (_, Shape::Vector) => Shape::Vector,
                            (Shape::Tensor, _) | (_, Shape::Tensor) => Shape::Tensor,
                            _ => Shape::Scalar,
                        };
                        Ok(Typed {
                            dim,
                            shape,
                            literal: left.literal && right.literal,
                        })
                    }
                    '/' => {
                        let dim = if left.literal && right.literal {
                            Dim::ZERO
                        } else if left.literal {
                            right.dim
                        } else if right.literal {
                            left.dim
                        } else {
                            left.dim.sub(&right.dim)
                        };
                        Ok(Typed {
                            dim,
                            shape: left.shape,
                            literal: left.literal && right.literal,
                        })
                    }
                    '^' => {
                        if right.shape == Shape::Vector {
                            return Err(UnitError::Shape("exponent must be a scalar".into()));
                        }
                        let exponent = match b.as_ref() {
                            Ast::Number(v) => *v as f32,
                            _ => 1.0,
                        };
                        Ok(Typed {
                            dim: if left.literal { Dim::ZERO } else { left.dim.scale(exponent) },
                            shape: Shape::Scalar,
                            literal: left.literal && right.literal,
                        })
                    }
                    other => Err(UnitError::Parse(format!("unknown operator {other}"))),
                }
            }
            Ast::Call { name, args } => {
                let typed: Result<Vec<Typed>, UnitError> = args.iter().map(|a| self.walk(a)).collect();
                let typed = typed?;
                let first = typed
                    .first()
                    .copied()
                    .ok_or_else(|| UnitError::Function(name.clone()))?;
                match name.as_str() {
                    "dot" => {
                        if typed.len() != 2 {
                            return Err(UnitError::Function("dot".into()));
                        }
                        if first.shape != Shape::Vector || typed[1].shape != Shape::Vector {
                            return Err(UnitError::Shape("dot() requires two vector fields".into()));
                        }
                        Ok(Typed::scalar(first.dim.add(&typed[1].dim)))
                    }
                    "cross" => {
                        if typed.len() != 2 {
                            return Err(UnitError::Function("cross".into()));
                        }
                        Ok(Typed::vector(first.dim.add(&typed[1].dim)))
                    }
                    "mag" => {
                        if first.shape == Shape::Scalar {
                            return Err(UnitError::Shape("mag() expects a vector".into()));
                        }
                        Ok(Typed::scalar(first.dim))
                    }
                    "mag_sq" => {
                        if first.shape == Shape::Scalar {
                            return Err(UnitError::Shape("mag_sq() expects a vector or tensor".into()));
                        }
                        Ok(Typed::scalar(first.dim.scale(2.0)))
                    }
                    "grad" => {
                        let dim = first.dim.sub(&LENGTH);
                        Ok(Typed {
                            dim,
                            shape: if first.shape == Shape::Scalar { Shape::Vector } else { Shape::Tensor },
                            literal: false,
                        })
                    }
                    "div" => {
                        if first.shape != Shape::Vector {
                            return Err(UnitError::Shape("div() expects a vector field".into()));
                        }
                        Ok(Typed::scalar(first.dim.sub(&LENGTH)))
                    }
                    "curl" => {
                        if first.shape != Shape::Vector {
                            return Err(UnitError::Shape("curl() expects a vector field".into()));
                        }
                        Ok(Typed::vector(first.dim.sub(&LENGTH)))
                    }
                    "sqrt" => Ok(Typed {
                        dim: if first.literal { Dim::ZERO } else { first.dim.scale(0.5) },
                        shape: Shape::Scalar,
                        literal: first.literal,
                    }),
                    "abs" => Ok(first),
                    "exp" | "ln" | "log" => {
                        if !first.dim.is_zero() && !first.literal {
                            return Err(UnitError::Mismatch {
                                op: "evaluate a transcendental of",
                                left: describe(&first),
                                right: "Dimensionless [1]".into(),
                                hint: "divide by a reference quantity first".into(),
                            });
                        }
                        Ok(Typed::scalar(Dim::ZERO))
                    }
                    other => Err(UnitError::Function(other.to_string())),
                }
            }
        }
    }
}

fn format_ast(ast: &Ast) -> String {
    match ast {
        Ast::Number(v) => format!("{v}"),
        Ast::Var(v) => v.clone(),
        Ast::Neg(a) => format!("-{a:?}").replace("Number(", "").replace(')', ""),
        Ast::Bin { op, a, b } => format!("({} {op} {})", format_ast(a), format_ast(b)),
        Ast::Call { name, args } => {
            format!("{name}({})", args.iter().map(format_ast).collect::<Vec<_>>().join(", "))
        }
    }
}

#[derive(Clone, Debug)]
pub struct CompiledField {
    pub target: Option<String>,
    pub dim: Dim,
    pub quantity: &'static str,
    pub unit: &'static str,
    pub trace: Vec<TraceEntry>,
    pub body: String,
}

/// Compile a `name = expression` string against the SI registry.
pub fn compile_field(source: &str) -> Result<CompiledField, UnitError> {
    let (target, body) = match source.split_once('=') {
        Some((name, rest)) if !name.trim().is_empty() && !rest.trim().is_empty() => {
            (Some(name.trim().to_string()), rest.trim())
        }
        _ => (None, source.trim()),
    };
    let ast = Parser::new(body)?.parse()?;
    let registry = UnitRegistry::default();
    let mut trace = Vec::new();
    let typed = registry.check(&ast, &mut trace)?;
    if !typed.literal && typed.shape != Shape::Scalar {
        return Err(UnitError::NonScalarResult { shape: typed.shape });
    }
    let unit = match quantity_name(&typed.dim) {
        "Pressure" => "Pa",
        "Velocity" => "m·s⁻¹",
        "Temperature" => "K",
        "Specific kinetic energy" => "m²·s⁻²",
        "Density" => "kg·m⁻³",
        "Volumetric entropy generation" => "W·m⁻³·K⁻¹",
        "Rate" => "s⁻¹",
        _ => "—",
    };
    Ok(CompiledField {
        target,
        dim: typed.dim,
        quantity: typed.quantity(),
        unit,
        trace,
        body: body.to_string(),
    })
}

impl CompiledField {
    /// Rhai is the post-processing / UDF surface. The expression is embedded
    /// only after the SI checker has accepted it.
    pub fn rhai_upload_script(&self) -> String {
        let name = self.target.clone().unwrap_or_else(|| "derived_field".into());
        format!(
            "// SI = {dims}\nlet field = compile_checked(\"{body}\", \"{quantity}\");\nlet handle = upload_cfdfld03(field, \"{name}\", \"{unit}\");\nvisualize(handle, Colormap::OklabTurbo);",
            dims = self.dim.superscript(),
            body = self.body.replace('"', "\\\""),
            quantity = self.quantity,
            name = name,
            unit = self.unit,
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pressure_accumulation_is_valid() {
        let c = compile_field("p_total = p + 0.5 * rho * dot(U, U)").unwrap();
        assert_eq!(c.quantity, "Pressure");
        assert!(c.dim.is_close(&P));
    }

    #[test]
    fn missing_density_is_rejected_with_superscripts() {
        let err = compile_field("p + 0.5 * dot(U, U)").unwrap_err();
        let msg = err.to_string();
        assert!(msg.contains("Pressure"));
        assert!(msg.contains("Specific kinetic energy"));
        assert!(msg.contains("rho"));
        assert!(msg.contains("⁻²"));
    }

    #[test]
    fn mach_is_dimensionless() {
        let c = compile_field("mach = mag(U) / sqrt(gamma * R_air * T)").unwrap();
        assert!(c.dim.is_zero());
        assert!(c.body.contains("sqrt"));
    }

    #[test]
    fn entropy_generation_matches_both_terms() {
        let c = compile_field(
            "sgen = mu / T * mag_sq(grad(U)) + kappa / (T*T) * mag_sq(grad(T))",
        )
        .unwrap();
        assert_eq!(c.quantity, "Volumetric entropy generation");
    }
}
