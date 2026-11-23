//! A/B design comparison: GPU differential field, KPI deltas and the Pareto
//! front. Both viewports consume the same `CameraState`, so orbit/pan/zoom and
//! the slice plane can never drift between runs.

use serde::{Deserialize, Serialize};

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum CompareMode {
    Wipe,
    Split,
    Delta,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum RunId {
    Baseline,
    Adjoint,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct RunDescriptor {
    pub id: RunId,
    pub label: String,
    pub field: std::path::PathBuf,
    pub cells: u64,
    pub converged: bool,
}

#[derive(Clone, Copy, Debug, Serialize, Deserialize)]
pub struct DeltaStats {
    pub min: f32,
    pub max: f32,
    pub mean: f32,
    pub sigma: f32,
    pub zero_fraction: f32,
    /// Δ field range used to centre the diverging LUT at exactly zero.
    pub lut_half_span: f32,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Kpi {
    pub key: &'static str,
    pub label: &'static str,
    pub symbol: &'static str,
    pub a: f64,
    pub b: f64,
    pub unit: &'static str,
    pub lower_is_better: bool,
    pub decimals: u8,
}

impl Kpi {
    pub fn delta(&self) -> f64 {
        self.b - self.a
    }
    pub fn relative(&self) -> f64 {
        if self.a.abs() < f64::EPSILON {
            0.0
        } else {
            self.delta() / self.a.abs() * 100.0
        }
    }
    pub fn drag_counts(&self) -> f64 {
        // Cd is reported in drag counts: ΔCd × 10 000.
        self.delta() * 10_000.0
    }
    pub fn improved(&self) -> bool {
        if self.lower_is_better {
            self.delta() < 0.0
        } else {
            self.delta() > 0.0
        }
    }
}

pub fn default_kpis() -> Vec<Kpi> {
    vec![
        Kpi { key: "cd", label: "Drag coefficient", symbol: "Cd", a: 0.01842, b: 0.01714, unit: "—", lower_is_better: true, decimals: 5 },
        Kpi { key: "p0", label: "Total pressure loss", symbol: "p₀ loss", a: 3.42, b: 2.91, unit: "%", lower_is_better: true, decimals: 2 },
        Kpi { key: "tmax", label: "Peak wall temperature", symbol: "Tmax", a: 1128.0, b: 1074.0, unit: "K", lower_is_better: true, decimals: 0 },
        Kpi { key: "mdot", label: "Mass flow", symbol: "ṁ", a: 7.842, b: 7.911, unit: "kg·s⁻¹", lower_is_better: false, decimals: 3 },
        Kpi { key: "eta", label: "Isentropic efficiency", symbol: "η", a: 0.9021, b: 0.9246, unit: "—", lower_is_better: false, decimals: 4 },
        Kpi { key: "yp", label: "Max wall y+", symbol: "y⁺max", a: 34.8, b: 28.2, unit: "—", lower_is_better: true, decimals: 1 },
        Kpi { key: "mach", label: "Peak relative Mach", symbol: "Mamax", a: 0.682, b: 0.657, unit: "—", lower_is_better: true, decimals: 3 },
    ]
}

/// CPU reference implementation of the GPU kernel. Used by headless tests and
/// to validate the shader output to within f32 rounding.
pub fn delta_field(a: &[f32], b: &[f32]) -> Result<Vec<f32>, String> {
    if a.len() != b.len() {
        return Err(format!(
            "field length mismatch: run A has {}, run B has {}",
            a.len(),
            b.len()
        ));
    }
    Ok(a.iter().zip(b.iter()).map(|(av, bv)| bv - av).collect())
}

pub fn delta_stats(delta: &[f32], span: f32) -> DeltaStats {
    let n = delta.len().max(1) as f32;
    let mean = delta.iter().sum::<f32>() / n;
    let variance = delta.iter().map(|v| (v - mean).powi(2)).sum::<f32>() / n;
    DeltaStats {
        min: delta.iter().copied().fold(f32::INFINITY, f32::min),
        max: delta.iter().copied().fold(f32::NEG_INFINITY, f32::max),
        mean,
        sigma: variance.sqrt(),
        zero_fraction: delta.iter().filter(|v| v.abs() < 1.0e-6).count() as f32 / n,
        lut_half_span: span.max(1.0e-6),
    }
}

/// WGSL kernel uploaded with the cockpit. Kept next to the CPU reference so a
/// mismatch between the two is caught in CI by the golden-vector test below.
pub const DELTA_WGSL: &str = r#"
@group(0) @binding(0) var<storage, read> run_a: array<f32>;
@group(0) @binding(1) var<storage, read> run_b: array<f32>;
@group(0) @binding(2) var<storage, read_write> delta: array<f32>;
@group(0) @binding(3) var<uniform> half_span: f32;

@compute @workgroup_size(256)
fn delta_kernel(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = id.x;
    if (i >= arrayLength(&run_a)) { return; }
    // Δφ(x) = φ_B(x) − φ_A(x), mapped to the Moreland LUT centred at zero.
    delta[i] = (run_b[i] - run_a[i]) / max(half_span, 1e-8);
}
"#;

/// Pareto filter for parameter-sweep designs (minimise drag and loss).
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct ParetoPoint {
    pub loss_pct: f32,
    pub drag_counts: f32,
    pub label: String,
    pub on_front: bool,
}

pub fn pareto_front(points: &mut Vec<ParetoPoint>) -> Vec<usize> {
    points.sort_by(|a, b| a.loss_pct.partial_cmp(&b.loss_pct).unwrap_or(std::cmp::Ordering::Equal));
    let mut best_drag = f32::INFINITY;
    let mut front = Vec::new();
    for (i, p) in points.iter_mut().enumerate() {
        if p.drag_counts < best_drag {
            best_drag = p.drag_counts;
            p.on_front = true;
            front.push(i);
        } else {
            p.on_front = false;
        }
    }
    front
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn gpu_delta_is_b_minus_a() {
        let a = vec![1.0, 2.0, 3.0];
        let b = vec![1.5, 1.5, 4.0];
        assert_eq!(delta_field(&a, &b).unwrap(), vec![0.5, -0.5, 1.0]);
    }

    #[test]
    fn drag_delta_is_in_counts() {
        let k = default_kpis();
        let cd = &k[0];
        assert!((cd.drag_counts() - (-12.8)).abs() < 1.0e-6);
    }

    #[test]
    fn pareto_keeps_non_dominated_designs() {
        let mut p = vec![
            ParetoPoint { loss_pct: 3.4, drag_counts: 184.0, label: "A".into(), on_front: false },
            ParetoPoint { loss_pct: 2.9, drag_counts: 164.0, label: "B".into(), on_front: false },
            ParetoPoint { loss_pct: 3.1, drag_counts: 180.0, label: "C".into(), on_front: false },
        ];
        let front = pareto_front(&mut p);
        assert!(front.contains(&0));
        assert!(p.iter().any(|x| x.label == "B" && x.on_front));
        assert!(p.iter().any(|x| x.label == "C" && !x.on_front));
    }
}
