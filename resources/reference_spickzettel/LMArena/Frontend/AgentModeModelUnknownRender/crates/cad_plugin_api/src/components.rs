use bevy_ecs::prelude::*;
use cad_core::{CadMaterialData, MassProperties};
use glam::{Mat4, Vec3};

use crate::resources::MeshHandle;

#[derive(Component, Debug, Clone)]
pub struct CadPart {
    pub name: String,
    pub group: String,
    pub mesh: MeshHandle,
}

#[derive(Component, Debug, Clone, Copy)]
pub struct PartTransform {
    pub base_matrix: Mat4,
    pub explode_offset: Vec3,
}

impl Default for PartTransform {
    fn default() -> Self {
        Self {
            base_matrix: Mat4::IDENTITY,
            explode_offset: Vec3::ZERO,
        }
    }
}

#[derive(Component, Debug, Clone, Copy, Default)]
pub struct WorldTransform(pub Mat4);

#[derive(Component, Debug, Clone, Copy, Default)]
pub struct CadMaterial(pub CadMaterialData);

/// Non-zero GPU picking identifier mapped to this entity.
#[derive(Component, Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct PickableId(pub u32);

#[derive(Component, Debug, Clone, Copy)]
pub struct PartVisibility {
    pub visible: bool,
    pub isolated_hidden: bool,
}

impl Default for PartVisibility {
    fn default() -> Self {
        Self {
            visible: true,
            isolated_hidden: false,
        }
    }
}

impl PartVisibility {
    pub fn is_drawn(&self) -> bool {
        self.visible && !self.isolated_hidden
    }
}

#[derive(Component, Debug, Clone, Copy, Default)]
pub struct SelectionState {
    pub selected: bool,
    pub hovered: bool,
}

#[derive(Component, Debug, Clone)]
pub struct AddonOwned {
    pub addon_id: String,
}

#[derive(Component, Debug, Clone, Copy, Default)]
pub struct PartMassCache(pub Option<MassProperties>);
