//! Click-to-author spatial boundary rules.
//!
//! The pick buffer (`R32Uint`) only returns a `face_group_id`. That integer is
//! ephemeral: it is valid for one tessellation of one STEP body. A rule that is
//! stored as `face_group_id == 7` will silently rebind after a CAD update.
//!
//! `SmartSpatialRuleSynth` turns the picked patch into a topology-independent
//! predicate from geometry alone (normal cone, centroid, AABB, cylindrical
//! radius, STEP tag). The resulting Rhai expression is deterministic and is
//! evaluated in the solver pre-processor, not baked into the mesh.

use glam::{Vec2, Vec3};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord, Serialize, Deserialize)]
pub struct FaceGroupId(pub u32);

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum Axis {
    X,
    Y,
    Z,
}

impl Axis {
    pub fn index(self) -> usize {
        match self {
            Axis::X => 0,
            Axis::Y => 1,
            Axis::Z => 2,
        }
    }
    pub fn component(self, v: Vec3) -> f32 {
        v[self.index()]
    }
    pub fn name(self) -> &'static str {
        match self {
            Axis::X => "x",
            Axis::Y => "y",
            Axis::Z => "z",
        }
    }
}

#[derive(Clone, Debug)]
pub struct FaceSample {
    pub group: FaceGroupId,
    pub centroid: Vec3,
    pub normal: Vec3,
    pub area: f32,
}

#[derive(Clone, Debug, Default)]
pub struct Aabb {
    pub min: Vec3,
    pub max: Vec3,
}

impl Aabb {
    pub fn from_points(points: impl Iterator<Item = Vec3>) -> Self {
        let mut min = Vec3::splat(f32::INFINITY);
        let mut max = Vec3::splat(f32::NEG_INFINITY);
        for p in points {
            min = min.min(p);
            max = max.max(p);
        }
        Self { min, max }
    }
    pub fn extent(&self) -> Vec3 {
        self.max - self.min
    }
    pub fn limit(&self, axis: Axis, upper: bool) -> f32 {
        if upper {
            axis.component(self.max)
        } else {
            axis.component(self.min)
        }
    }
}

#[derive(Clone, Debug, Default)]
pub struct PatchTopology {
    pub group: FaceGroupId,
    pub step_tag: String,
    pub faces: Vec<FaceSample>,
}

impl PatchTopology {
    /// Area-weighted mean normal and the half-angle of the enclosing cone.
    pub fn normal_cone(&self) -> (Vec3, f32) {
        let mut sum = Vec3::ZERO;
        let mut area = 0.0;
        for f in &self.faces {
            sum += f.normal * f.area;
            area += f.area;
        }
        let mean = if sum.length_squared() > 1e-12 {
            sum.normalize_or_zero()
        } else {
            Vec3::Y
        };
        let widest = self
            .faces
            .iter()
            .map(|f| mean.dot(f.normal).clamp(-1.0, 1.0).acos())
            .fold(0.0_f32, f32::max);
        (mean, widest.to_degrees())
    }

    pub fn centroid(&self) -> Vec3 {
        let mut sum = Vec3::ZERO;
        let mut area = 0.0;
        for f in &self.faces {
            sum += f.centroid * f.area;
            area += f.area;
        }
        if area > 0.0 {
            sum / area
        } else {
            Vec3::ZERO
        }
    }

    pub fn bbox(&self) -> Aabb {
        Aabb::from_points(self.faces.iter().map(|f| f.centroid))
    }

    pub fn area(&self) -> f32 {
        self.faces.iter().map(|f| f.area).sum()
    }

    /// Radius around an arbitrary axis. `origin` is the two coordinates
    /// perpendicular to `axis` (e.g. for a rotor spinning about +Y the pair is
    /// (x, z)). Used to recognise a cylindrical leading edge / fillet patch
    /// independently of its position along the axis.
    pub fn cylindrical_radius(&self, axis: Axis, origin: Vec2) -> (f32, f32) {
        let mut min = f32::INFINITY;
        let mut max: f32 = 0.0;
        for f in &self.faces {
            let (u, v) = match axis {
                Axis::X => (f.centroid.y, f.centroid.z),
                Axis::Y => (f.centroid.x, f.centroid.z),
                Axis::Z => (f.centroid.x, f.centroid.y),
            };
            let r = ((u - origin.x).powi(2) + (v - origin.y).powi(2)).sqrt();
            min = min.min(r);
            max = max.max(r);
        }
        if !min.is_finite() {
            min = 0.0;
        }
        (min, max)
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub enum Predicate {
    NormalCone {
        axis: Axis,
        threshold: f32,
    },
    AxisBand {
        axis: Axis,
        center: f32,
        tolerance: f32,
    },
    CylRadius {
        axis: Axis,
        origin: Vec2,
        range: [f32; 2],
    },
    BboxLimit {
        axis: Axis,
        upper: bool,
        limit: f32,
    },
    And(Vec<Predicate>),
    Or(Vec<Predicate>),
}

impl Predicate {
    pub fn matches(&self, face: &FaceSample) -> bool {
        match self {
            Predicate::NormalCone { axis, threshold } => {
                let n = axis.component(face.normal);
                if *threshold < 0.0 {
                    n < *threshold
                } else {
                    n > *threshold
                }
            }
            Predicate::AxisBand {
                axis,
                center,
                tolerance,
            } => (axis.component(face.centroid) - center).abs() < *tolerance,
            Predicate::CylRadius {
                axis,
                origin,
                range,
            } => {
                let (u, v) = match axis {
                    Axis::X => (face.centroid.y, face.centroid.z),
                    Axis::Y => (face.centroid.x, face.centroid.z),
                    Axis::Z => (face.centroid.x, face.centroid.y),
                };
                let r = ((u - origin.x).powi(2) + (v - origin.y).powi(2)).sqrt();
                r >= range[0] && r <= range[1]
            }
            Predicate::BboxLimit { axis, upper, limit } => {
                let v = axis.component(face.centroid);
                if *upper {
                    v > *limit
                } else {
                    v < *limit
                }
            }
            Predicate::And(parts) => parts.iter().all(|p| p.matches(face)),
            Predicate::Or(parts) => parts.iter().any(|p| p.matches(face)),
        }
    }

    pub fn to_rhai(&self) -> String {
        match self {
            Predicate::NormalCone { axis, threshold } => {
                format!("normal.{} {} {:.3}", axis.name(), if *threshold < 0.0 { "<" } else { ">" }, threshold.abs())
            }
            Predicate::AxisBand {
                axis,
                center,
                tolerance,
            } => format!("abs({} - {:.4}) < 1e-3 * {:.3}", axis.name(), center, tolerance.max(1.0)),
            Predicate::CylRadius { range, .. } => {
                format!("cyl_radius_yz(0, 0) in [{:.5}, {:.5}]", range[0], range[1])
            }
            Predicate::BboxLimit { axis, upper, limit } => {
                let field = if *upper { "bbox.max" } else { "bbox.min" };
                format!("{field}.{} {} {:.5}", axis.name(), if *upper { ">" } else { "<" }, limit)
            }
            Predicate::And(parts) => parts.iter().map(Predicate::to_rhai).collect::<Vec<_>>().join(" and "),
            Predicate::Or(parts) => parts.iter().map(Predicate::to_rhai).collect::<Vec<_>>().join(" or "),
        }
    }
}

#[derive(Clone, Debug)]
pub struct ProposedRule {
    pub source_group: FaceGroupId,
    pub step_tag: String,
    pub predicate: Predicate,
    pub rhai: String,
    pub confidence: f32,
    pub matched_faces: usize,
    pub matched_area: f32,
    pub normal_cone_deg: f32,
    pub centroid: Vec3,
    pub bbox: Aabb,
    pub topology_independent: bool,
}

#[derive(Clone, Debug)]
pub struct SmartSpatialRuleSynth {
    pub normal_cos_tolerance: f32,
    pub linear_tolerance: f32,
    pub minimum_confidence: f32,
}

impl Default for SmartSpatialRuleSynth {
    fn default() -> Self {
        Self {
            normal_cos_tolerance: 0.995,
            linear_tolerance: 1.0e-3,
            minimum_confidence: 0.90,
        }
    }
}

impl SmartSpatialRuleSynth {
    pub fn synthesize(&self, patch: &PatchTopology) -> ProposedRule {
        let (n, cone) = patch.normal_cone();
        let centroid = patch.centroid();
        let bbox = patch.bbox();
        let dominant = dominant_axis(n);
        let tag = patch.step_tag.to_ascii_uppercase();

        // 1. Cylindrical patches (leading edge, fillet, bleed holes) are best
        //    described by a radius band, never by an axis-aligned box.
        let (r_min, r_max) = patch.cylindrical_radius(Axis::Y, Vec2::new(0.0, 0.0));
        let cylindrical = tag.contains("LEADING")
            || tag.contains("FILLET")
            || tag.contains("HOLE")
            || (r_max - r_min < 0.004 * r_max.max(1.0) && r_max > 0.0);

        let mut parts: Vec<Predicate> = Vec::new();
        if cylindrical {
            parts.push(Predicate::CylRadius {
                axis: Axis::Y,
                origin: Vec2::ZERO,
                range: [r_min, r_max],
            });
            parts.push(Predicate::NormalCone {
                axis: dominant,
                threshold: if n[dominant.index()] < 0.0 {
                    -(self.normal_cos_tolerance.min(0.32))
                } else {
                    self.normal_cos_tolerance.min(0.32)
                },
            });
        } else if tag.contains("TIP") || tag.contains("HUB") || tag.contains("SHROUD") {
            // Revolved end walls: a tight band along the axis plus a normal test.
            parts.push(Predicate::AxisBand {
                axis: Axis::Y,
                center: centroid.y,
                tolerance: self.linear_tolerance,
            });
            parts.push(Predicate::NormalCone {
                axis: Axis::Y,
                threshold: if n.y < 0.0 {
                    -self.normal_cos_tolerance
                } else {
                    self.normal_cos_tolerance
                },
            });
        } else if tag.contains("TRAILING") {
            parts.push(Predicate::NormalCone {
                axis: Axis::X,
                threshold: self.normal_cos_tolerance,
            });
            parts.push(Predicate::BboxLimit {
                axis: Axis::X,
                upper: true,
                limit: bbox.limit(Axis::X, true) - 0.012,
            });
        } else {
            // Aerodynamic surfaces: normal cone + span band.
            parts.push(Predicate::NormalCone {
                axis: dominant,
                threshold: if n[dominant.index()] < 0.0 {
                    -(self.normal_cos_tolerance)
                } else {
                    self.normal_cos_tolerance
                },
            });
            let span_axis = if dominant == Axis::Y { Axis::X } else { Axis::Y };
            parts.push(Predicate::AxisBand {
                axis: span_axis,
                center: span_axis.component(centroid),
                tolerance: (0.002_f32).max(bbox.extent()[span_axis.index()] * 0.02),
            });
        }

        let predicate = Predicate::And(parts);
        let total = patch.faces.len().max(1);
        let matched_faces = patch.faces.iter().filter(|f| predicate.matches(f)).count();
        let match_ratio = matched_faces as f32 / total as f32;
        let cone_factor = (1.0 - (cone / 180.0)).clamp(0.0, 1.0);
        let confidence = (0.55 * match_ratio + 0.45 * cone_factor).clamp(0.0, 1.0) * 100.0;
        let matched_area = patch
            .faces
            .iter()
            .filter(|f| predicate.matches(f))
            .map(|f| f.area)
            .sum();

        ProposedRule {
            source_group: patch.group,
            step_tag: patch.step_tag.clone(),
            rhai: predicate.to_rhai(),
            predicate,
            confidence,
            matched_faces,
            matched_area,
            normal_cone_deg: cone,
            centroid,
            bbox,
            topology_independent: true,
        }
    }
}

fn dominant_axis(n: Vec3) -> Axis {
    let a = n.abs();
    if a.x >= a.y && a.x >= a.z {
        Axis::X
    } else if a.y >= a.z {
        Axis::Y
    } else {
        Axis::Z
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum BcKind {
    Inlet,
    Outlet,
    NoSlipWall,
    MovingWall,
    Interface,
    Symmetry,
    Porous,
    Source,
}

#[derive(Clone, Debug)]
pub struct BoundaryRule {
    pub id: String,
    pub name: String,
    pub kind: BcKind,
    pub groups: Vec<FaceGroupId>,
    pub predicate: Option<Predicate>,
    pub physical_target: String,
    pub enabled: bool,
}

#[derive(Clone, Debug)]
pub struct Overlap {
    pub face_group: FaceGroupId,
    pub a: String,
    pub b: String,
    pub area: f32,
}

/// Detect faces claimed by more than one active boundary condition. The
/// viewport colours the intersected face groups in warning red.
pub fn detect_overlaps(
    rules: &[BoundaryRule],
    faces: &[FaceSample],
) -> Vec<Overlap> {
    let active: Vec<&BoundaryRule> = rules.iter().filter(|r| r.enabled).collect();
    let mut by_group: HashMap<FaceGroupId, Vec<&BoundaryRule>> = HashMap::new();
    for face in faces {
        for rule in &active {
            let claims = rule.groups.contains(&face.group)
                || rule.predicate.as_ref().is_some_and(|p| p.matches(face));
            if claims {
                by_group.entry(face.group).or_default().push(rule);
            }
        }
    }
    let mut out = Vec::new();
    for (group, mut owners) in by_group {
        owners.sort_by(|a, b| a.id.cmp(&b.id));
        owners.dedup_by(|a, b| a.id == b.id);
        if owners.len() > 1 {
            let area = faces
                .iter()
                .filter(|f| f.group == group)
                .map(|f| f.area)
                .sum();
            out.push(Overlap {
                face_group: group,
                a: owners[0].id.clone(),
                b: owners[1].id.clone(),
                area,
            });
        }
    }
    out.sort_by_key(|o| o.face_group);
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    fn blade_patch() -> PatchTopology {
        let mut faces = Vec::new();
        for i in 0..64 {
            let t = i as f32 / 63.0;
            let normal = Vec3::new(0.36, 0.0, 0.93).normalize();
            faces.push(FaceSample {
                group: FaceGroupId(2),
                centroid: Vec3::new(-0.8 + 1.6 * t, 0.04, 0.19),
                normal,
                area: 0.004,
            });
        }
        PatchTopology {
            group: FaceGroupId(2),
            step_tag: "SURF_SUCTION".into(),
            faces,
        }
    }

    #[test]
    fn synthesizes_topology_independent_rule() {
        let synth = SmartSpatialRuleSynth::default();
        let rule = synth.synthesize(&blade_patch());
        assert!(rule.topology_independent);
        assert!(rule.rhai.contains("normal."));
        assert!(rule.matched_faces > 0);
        assert!(rule.confidence > 50.0);
    }

    #[test]
    fn detects_bc_overlap() {
        let patch = blade_patch();
        let rules = vec![
            BoundaryRule {
                id: "R1".into(),
                name: "adiabatic".into(),
                kind: BcKind::NoSlipWall,
                groups: vec![FaceGroupId(2)],
                predicate: None,
                physical_target: String::new(),
                enabled: true,
            },
            BoundaryRule {
                id: "R2".into(),
                name: "heated".into(),
                kind: BcKind::NoSlipWall,
                groups: vec![FaceGroupId(2)],
                predicate: None,
                physical_target: String::new(),
                enabled: true,
            },
        ];
        let overlaps = detect_overlaps(&rules, &patch.faces);
        assert_eq!(overlaps.len(), 1);
        assert_eq!(overlaps[0].a, "R1");
    }
}
