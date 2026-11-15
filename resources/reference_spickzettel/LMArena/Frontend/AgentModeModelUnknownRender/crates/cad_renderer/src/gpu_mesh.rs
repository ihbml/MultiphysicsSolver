use cad_core::CadMesh;
use cad_plugin_api::{MeshHandle, MeshLibrary};
use wgpu::util::DeviceExt;

pub struct GpuMesh {
    pub vertex_buffer: wgpu::Buffer,
    pub index_buffer: wgpu::Buffer,
    pub index_count: u32,
    pub edge_buffer: Option<wgpu::Buffer>,
    pub edge_vertex_count: u32,
}

#[derive(Default)]
pub struct GpuMeshCache {
    meshes: Vec<GpuMesh>,
    synced_revision: u64,
}

impl GpuMeshCache {
    pub fn sync_from_library(&mut self, device: &wgpu::Device, library: &MeshLibrary) {
        if self.synced_revision == library.revision() && self.meshes.len() == library.len() {
            return;
        }
        self.meshes.clear();
        for (_, mesh) in library.iter() {
            self.meshes.push(upload_mesh(device, mesh));
        }
        self.synced_revision = library.revision();
    }

    pub fn get(&self, handle: MeshHandle) -> Option<&GpuMesh> {
        self.meshes.get(handle.0)
    }
}

fn upload_mesh(device: &wgpu::Device, mesh: &CadMesh) -> GpuMesh {
    let vertex_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
        label: Some(&format!("{}_vb", mesh.name)),
        contents: bytemuck::cast_slice(&mesh.vertices),
        usage: wgpu::BufferUsages::VERTEX,
    });
    let index_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
        label: Some(&format!("{}_ib", mesh.name)),
        contents: bytemuck::cast_slice(&mesh.indices),
        usage: wgpu::BufferUsages::INDEX,
    });

    let edge_buffer = if !mesh.edge_vertices.is_empty() {
        Some(device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some(&format!("{}_edges", mesh.name)),
            contents: bytemuck::cast_slice(&mesh.edge_vertices),
            usage: wgpu::BufferUsages::VERTEX,
        }))
    } else {
        None
    };

    GpuMesh {
        vertex_buffer,
        index_buffer,
        index_count: mesh.indices.len() as u32,
        edge_buffer,
        edge_vertex_count: mesh.edge_vertices.len() as u32,
    }
}
