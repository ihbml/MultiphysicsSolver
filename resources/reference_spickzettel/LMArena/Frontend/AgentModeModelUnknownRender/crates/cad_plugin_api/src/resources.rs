use std::collections::BTreeMap;
use std::path::PathBuf;

use bevy_ecs::prelude::*;
use cad_core::{Aabb3, CadMesh, DemoAssemblyKind, OrbitCameraMath, Plane3};
use glam::{Mat4, Vec3};
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub struct MeshHandle(pub usize);

#[derive(Resource, Debug, Default)]
pub struct MeshLibrary {
    meshes: Vec<CadMesh>,
    revision: u64,
}

impl MeshLibrary {
    pub fn add_mesh(&mut self, mesh: CadMesh) -> MeshHandle {
        let idx = self.meshes.len();
        self.meshes.push(mesh);
        self.revision += 1;
        MeshHandle(idx)
    }

    pub fn get(&self, handle: MeshHandle) -> Option<&CadMesh> {
        self.meshes.get(handle.0)
    }

    pub fn iter(&self) -> impl Iterator<Item = (MeshHandle, &CadMesh)> {
        self.meshes
            .iter()
            .enumerate()
            .map(|(i, m)| (MeshHandle(i), m))
    }

    pub fn len(&self) -> usize {
        self.meshes.len()
    }

    pub fn is_empty(&self) -> bool {
        self.meshes.is_empty()
    }

    pub fn revision(&self) -> u64 {
        self.revision
    }

    pub fn clear(&mut self) {
        self.meshes.clear();
        self.revision += 1;
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum CameraPreset {
    Isometric,
    Top,
    Bottom,
    Front,
    Back,
    Left,
    Right,
}

#[derive(Resource, Debug, Clone)]
pub struct CameraState {
    pub orbit: OrbitCameraMath,
    pub turntable_active: bool,
    pub turntable_speed: f32,
}

impl Default for CameraState {
    fn default() -> Self {
        Self {
            orbit: OrbitCameraMath::default(),
            turntable_active: false,
            turntable_speed: 0.35,
        }
    }
}

impl CameraState {
    pub fn apply_preset(&mut self, preset: CameraPreset) {
        match preset {
            CameraPreset::Isometric => {
                self.orbit.yaw = 0.72;
                self.orbit.pitch = 0.48;
            }
            CameraPreset::Top => {
                self.orbit.yaw = 0.0;
                self.orbit.pitch = 1.555;
            }
            CameraPreset::Bottom => {
                self.orbit.yaw = 0.0;
                self.orbit.pitch = -1.555;
            }
            CameraPreset::Front => {
                self.orbit.yaw = std::f32::consts::FRAC_PI_2;
                self.orbit.pitch = 0.0;
            }
            CameraPreset::Back => {
                self.orbit.yaw = -std::f32::consts::FRAC_PI_2;
                self.orbit.pitch = 0.0;
            }
            CameraPreset::Right => {
                self.orbit.yaw = 0.0;
                self.orbit.pitch = 0.0;
            }
            CameraPreset::Left => {
                self.orbit.yaw = std::f32::consts::PI;
                self.orbit.pitch = 0.0;
            }
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[repr(u32)]
pub enum ShadingMode {
    ShadedWithEdges = 0,
    ShadedSmooth = 1,
    GoochTechnical = 2,
    MatCapCurvature = 3,
    ZebraStripes = 4,
    XRayGhost = 5,
    WireframeHidden = 6,
}

impl ShadingMode {
    pub const ALL: [Self; 7] = [
        Self::ShadedWithEdges,
        Self::ShadedSmooth,
        Self::GoochTechnical,
        Self::MatCapCurvature,
        Self::ZebraStripes,
        Self::XRayGhost,
        Self::WireframeHidden,
    ];

    pub fn label(&self) -> &'static str {
        match self {
            Self::ShadedWithEdges => "Shaded + Crease",
            Self::ShadedSmooth => "Studio PBR-Lite",
            Self::GoochTechnical => "Gooch Technical",
            Self::MatCapCurvature => "MatCap Curvature",
            Self::ZebraStripes => "Zebra Continuity",
            Self::XRayGhost => "X-Ray Inspection",
            Self::WireframeHidden => "Crease Wireframe",
        }
    }
}

#[derive(Resource, Debug, Clone)]
pub struct RenderSettings {
    pub shading_mode: ShadingMode,
    pub show_grid: bool,
    pub show_edges: bool,
    pub edge_bias: f32,
    pub msaa_samples: u32,
    pub background_top: [f32; 3],
    pub background_bottom: [f32; 3],
    pub exposure: f32,
    pub xray_alpha: f32,
    pub isolate_selected: bool,
}

impl Default for RenderSettings {
    fn default() -> Self {
        Self {
            shading_mode: ShadingMode::ShadedWithEdges,
            show_grid: true,
            show_edges: true,
            edge_bias: 0.00018,
            msaa_samples: 4,
            background_top: [0.095, 0.112, 0.140],
            background_bottom: [0.055, 0.065, 0.082],
            exposure: 1.05,
            xray_alpha: 0.30,
            isolate_selected: false,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum SectionAxis {
    X,
    Y,
    Z,
    Custom,
}

#[derive(Resource, Debug, Clone)]
pub struct SectionPlaneState {
    pub enabled: bool,
    pub axis: SectionAxis,
    pub custom_normal: Vec3,
    pub offset: f32,
    pub flip: bool,
    pub show_frame: bool,
}

impl Default for SectionPlaneState {
    fn default() -> Self {
        Self {
            enabled: false,
            axis: SectionAxis::Z,
            custom_normal: Vec3::new(1.0, 0.4, 0.6).normalize(),
            offset: 0.0,
            flip: false,
            show_frame: true,
        }
    }
}

impl SectionPlaneState {
    pub fn active_normal(&self) -> Vec3 {
        let base = match self.axis {
            SectionAxis::X => Vec3::X,
            SectionAxis::Y => Vec3::Y,
            SectionAxis::Z => Vec3::Z,
            SectionAxis::Custom => self.custom_normal.normalize_or(Vec3::Y),
        };
        if self.flip { -base } else { base }
    }

    pub fn to_plane3(&self) -> Option<Plane3> {
        if !self.enabled {
            return None;
        }
        let n = self.active_normal();
        let off = if self.flip { -self.offset } else { self.offset };
        Some(Plane3::new(n, off))
    }
}

#[derive(Resource, Debug, Clone)]
pub struct ExplodeState {
    pub factor: f32,
    pub animate: bool,
    pub speed: f32,
}

impl Default for ExplodeState {
    fn default() -> Self {
        Self {
            factor: 0.0,
            animate: false,
            speed: 0.55,
        }
    }
}

#[derive(Debug, Clone)]
pub struct MeasurementPoint {
    pub world_pos: Vec3,
    pub world_normal: Vec3,
    pub part_name: String,
}

#[derive(Resource, Debug, Default, Clone)]
pub struct MeasurementToolState {
    pub active: bool,
    pub points: Vec<MeasurementPoint>,
    pub last_distance: Option<f32>,
    pub last_angle_deg: Option<f32>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum BackendPreference {
    VulkanAndDx12,
    VulkanOnly,
    Dx12Only,
    AutoAll,
}

impl BackendPreference {
    pub fn label(&self) -> &'static str {
        match self {
            Self::VulkanAndDx12 => "DX12 + Vulkan",
            Self::VulkanOnly => "Vulkan",
            Self::Dx12Only => "DirectX 12",
            Self::AutoAll => "Auto Fallback",
        }
    }
}

#[derive(Resource, Debug, Clone)]
pub struct AdapterDetails {
    pub adapter_name: String,
    pub backend_name: String,
    pub driver: String,
    pub driver_info: String,
    pub device_type: String,
    pub vendor_id: u32,
    pub device_id: u32,
    pub msaa_samples: u32,
    pub reverse_z_enabled: bool,
    pub available_adapters: Vec<String>,
}

impl Default for AdapterDetails {
    fn default() -> Self {
        Self {
            adapter_name: "Uninitialized".into(),
            backend_name: "None".into(),
            driver: String::new(),
            driver_info: String::new(),
            device_type: String::new(),
            vendor_id: 0,
            device_id: 0,
            msaa_samples: 4,
            reverse_z_enabled: true,
            available_adapters: Vec::new(),
        }
    }
}

#[derive(Resource, Debug, Default, Clone)]
pub struct ViewportStats {
    pub fps: f32,
    pub frame_time_ms: f32,
    pub draw_calls: u32,
    pub visible_parts: usize,
    pub total_parts: usize,
    pub total_triangles: usize,
    pub total_edges: usize,
    pub hovered_pick_id: u32,
    pub hovered_part: Option<String>,
    pub selected_pick_id: u32,
    pub selected_part: Option<String>,
    pub cursor_world_pos: Option<Vec3>,
    pub scene_aabb: Aabb3,
}

#[derive(Debug, Clone, Copy)]
pub struct OverlayLine {
    pub start: Vec3,
    pub end: Vec3,
    pub color: [f32; 4],
    pub always_on_top: bool,
}

#[derive(Resource, Debug, Default)]
pub struct RenderOverlayQueue {
    pub lines: Vec<OverlayLine>,
}

impl RenderOverlayQueue {
    pub fn push_line(&mut self, start: Vec3, end: Vec3, color: [f32; 4], always_on_top: bool) {
        self.lines.push(OverlayLine {
            start,
            end,
            color,
            always_on_top,
        });
    }

    pub fn push_aabb(&mut self, aabb: &Aabb3, color: [f32; 4], always_on_top: bool) {
        if !aabb.is_valid() {
            return;
        }
        let min = aabb.min;
        let max = aabb.max;
        let corners = [
            Vec3::new(min.x, min.y, min.z),
            Vec3::new(max.x, min.y, min.z),
            Vec3::new(max.x, min.y, max.z),
            Vec3::new(min.x, min.y, max.z),
            Vec3::new(min.x, max.y, min.z),
            Vec3::new(max.x, max.y, min.z),
            Vec3::new(max.x, max.y, max.z),
            Vec3::new(min.x, max.y, max.z),
        ];
        let edges = [
            (0, 1),
            (1, 2),
            (2, 3),
            (3, 0),
            (4, 5),
            (5, 6),
            (6, 7),
            (7, 4),
            (0, 4),
            (1, 5),
            (2, 6),
            (3, 7),
        ];
        for (a, b) in edges {
            self.push_line(corners[a], corners[b], color, always_on_top);
        }
    }

    pub fn push_crosshair(&mut self, center: Vec3, radius: f32, color: [f32; 4]) {
        self.push_line(
            center - Vec3::X * radius,
            center + Vec3::X * radius,
            color,
            true,
        );
        self.push_line(
            center - Vec3::Y * radius,
            center + Vec3::Y * radius,
            color,
            true,
        );
        self.push_line(
            center - Vec3::Z * radius,
            center + Vec3::Z * radius,
            color,
            true,
        );
    }
}

#[derive(Debug, Clone)]
pub enum SceneCommand {
    LoadDemoAssembly(DemoAssemblyKind),
    LoadCadFile(PathBuf),
    SpawnPart {
        name: String,
        group: String,
        mesh: CadMesh,
        transform: Mat4,
        explode_offset: Vec3,
        color: [f32; 4],
        metallic: f32,
        roughness: f32,
        addon_id: Option<String>,
    },
    RemoveAddonParts(String),
    SelectPickId(Option<u32>),
    SetHoverPickId(Option<u32>),
    FocusAll,
    FocusSelected,
    SetPartColor {
        pick_id: u32,
        color: [f32; 4],
    },
    SetShadingMode(ShadingMode),
    SetSectionPlane {
        enabled: bool,
        normal: Vec3,
        offset: f32,
    },
}

#[derive(Resource, Debug, Default)]
pub struct SceneCommandQueue {
    pub commands: Vec<SceneCommand>,
}

impl SceneCommandQueue {
    pub fn push(&mut self, cmd: SceneCommand) {
        self.commands.push(cmd);
    }
}

#[derive(Resource, Debug, Default)]
pub struct CadTime {
    pub elapsed: f32,
    pub delta: f32,
    pub frame_count: u64,
}

#[derive(Message, Debug, Clone)]
pub enum CadEvent {
    PartPicked {
        pick_id: u32,
        name: String,
    },
    AssemblyLoaded {
        name: String,
        part_count: usize,
    },
    AddonAction {
        addon_id: String,
        action_id: String,
    },
}

#[derive(Debug, Clone)]
pub enum AddonWidget {
    Heading(String),
    Label(String),
    Readout {
        label: String,
        value: String,
    },
    Button {
        id: String,
        label: String,
    },
    Slider {
        id: String,
        label: String,
        min: f64,
        max: f64,
        default: f64,
    },
    Checkbox {
        id: String,
        label: String,
        default: bool,
    },
    Separator,
}

#[derive(Debug, Clone)]
pub struct AddonPanelSpec {
    pub addon_id: String,
    pub title: String,
    pub widgets: Vec<AddonWidget>,
}

#[derive(Debug, Clone)]
pub struct AddonUiEvent {
    pub addon_id: String,
    pub widget_id: String,
}

#[derive(Resource, Debug, Default)]
pub struct AddonPanelRegistry {
    pub panels: Vec<AddonPanelSpec>,
    pub float_values: BTreeMap<(String, String), f64>,
    pub bool_values: BTreeMap<(String, String), bool>,
    pub clicked_buttons: Vec<AddonUiEvent>,
}

impl AddonPanelRegistry {
    pub fn get_float(&self, addon_id: &str, id: &str, default: f64) -> f64 {
        self.float_values
            .get(&(addon_id.to_string(), id.to_string()))
            .copied()
            .unwrap_or(default)
    }

    pub fn get_bool(&self, addon_id: &str, id: &str, default: bool) -> bool {
        self.bool_values
            .get(&(addon_id.to_string(), id.to_string()))
            .copied()
            .unwrap_or(default)
    }
}

#[derive(Debug, Clone)]
pub struct AddonLogEntry {
    pub addon_id: String,
    pub message: String,
    pub is_error: bool,
}

#[derive(Debug, Clone)]
pub struct AddonStatusEntry {
    pub id: String,
    pub name: String,
    pub version: String,
    pub author: String,
    pub description: String,
    pub kind_label: String,
    pub enabled: bool,
    pub last_error: Option<String>,
    pub source_path: Option<PathBuf>,
    pub script_source: Option<String>,
}

#[derive(Resource, Debug, Default)]
pub struct AddonRegistry {
    pub entries: Vec<AddonStatusEntry>,
    pub logs: Vec<AddonLogEntry>,
}

impl AddonRegistry {
    pub fn log(&mut self, addon_id: impl Into<String>, msg: impl Into<String>, is_error: bool) {
        self.logs.push(AddonLogEntry {
            addon_id: addon_id.into(),
            message: msg.into(),
            is_error,
        });
        if self.logs.len() > 250 {
            let drain = self.logs.len() - 250;
            self.logs.drain(0..drain);
        }
    }

    pub fn is_enabled(&self, addon_id: &str) -> bool {
        self.entries
            .iter()
            .find(|e| e.id == addon_id)
            .is_some_and(|e| e.enabled)
    }
}
