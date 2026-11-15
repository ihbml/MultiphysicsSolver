//! Native Chunked Binary Container Format .mphys (SUGGESTIONS_BACKEND.md §11.2).

pub const MPHYS_HEADER_MAGIC: &[u8; 8] = b"MPHYS001";
pub const MPHYS_FOOTER_MAGIC: &[u8; 8] = b"MPHYSEOF";
pub const HEADER_SIZE_BYTES: usize = 64;
pub const DEFAULT_CHUNK_SIZE: usize = 65536;

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct MPhysHeader {
    pub magic: [u8; 8],
    pub version: u16,
    pub endianness: u16,
    pub footer_offset: u64,
    pub mesh_epoch_id: u64,
    pub flags: u32,
    pub chunk_size: u32,
    pub num_mesh_chunks: u32,
    pub num_field_steps: u32,
    pub _reserved: [u8; 20],
}

impl Default for MPhysHeader {
    fn default() -> Self {
        Self {
            magic: *MPHYS_HEADER_MAGIC,
            version: 1,
            endianness: 0x0100,
            footer_offset: 0,
            mesh_epoch_id: 0,
            flags: 0,
            chunk_size: DEFAULT_CHUNK_SIZE as u32,
            num_mesh_chunks: 0,
            num_field_steps: 0,
            _reserved: [0u8; 20],
        }
    }
}

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct MPhysFooterTrailer {
    pub footer_size: u64,
    pub magic: [u8; 8],
}
