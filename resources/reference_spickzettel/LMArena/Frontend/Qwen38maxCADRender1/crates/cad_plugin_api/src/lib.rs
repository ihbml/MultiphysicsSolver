pub mod addon;
pub mod components;
pub mod resources;

pub use addon::{
    AddonDescriptor, AddonKind, CadAddon, CadPluginHost, CadSchedule,
    spawn_assembly_parts, spawn_imported_assembly,
};
pub use components::{
    AddonOwned, CadMaterial, CadPart, PartMassCache, PartTransform, PartVisibility, PickableId,
    SelectionState, WorldTransform,
};
pub use resources::{
    AdapterDetails, AddonLogEntry, AddonPanelRegistry, AddonPanelSpec, AddonRegistry,
    AddonStatusEntry, AddonUiEvent, AddonWidget, BackendPreference, CadEvent, CadTime,
    CameraPreset, CameraState, ExplodeState, MeasurementPoint, MeasurementToolState, MeshHandle,
    MeshLibrary, OverlayLine, RenderOverlayQueue, RenderSettings, SceneCommand,
    SceneCommandQueue, SectionAxis, SectionPlaneState, ShadingMode, ViewportStats,
};
