//! ffi.rs - C-ABI compatibility layer for Python ctypes, C++20, and Modern Fortran.

use crate::mesh::FoamMesh;
use crate::parser::load_poly_mesh;
use std::cell::RefCell;
use std::ffi::{CStr, CString};
use std::os::raw::c_char;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::path::Path;

thread_local! {
    static LAST_ERROR: RefCell<Option<CString>> = const { RefCell::new(None) };
}

fn set_last_error(msg: impl AsRef<str>) {
    let clean = msg.as_ref().replace('\0', " ");
    LAST_ERROR.with(|slot| {
        *slot.borrow_mut() = CString::new(clean).ok();
    });
}

#[no_mangle]
pub unsafe extern "C" fn foam_last_error() -> *const c_char {
    LAST_ERROR.with(|slot| match slot.borrow().as_ref() {
        Some(c) => c.as_ptr(),
        None => std::ptr::null(),
    })
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_load(dir_path: *const c_char) -> *mut FoamMesh {
    catch_unwind(AssertUnwindSafe(|| {
        if dir_path.is_null() {
            set_last_error("foam_mesh_load received null path");
            return std::ptr::null_mut();
        }
        let c_str = match CStr::from_ptr(dir_path).to_str() {
            Ok(s) => s,
            Err(e) => {
                set_last_error(format!("Invalid UTF-8 path: {}", e));
                return std::ptr::null_mut();
            }
        };
        let path = Path::new(c_str);
        match load_poly_mesh(path) {
            Ok(mut mesh) => {
                mesh.sync_i64_indices();
                Box::into_raw(Box::new(mesh))
            }
            Err(e) => {
                set_last_error(e);
                std::ptr::null_mut()
            }
        }
    }))
    .unwrap_or_else(|_| {
        set_last_error("Panic caught in foam_mesh_load");
        std::ptr::null_mut()
    })
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_free(ptr: *mut FoamMesh) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if !ptr.is_null() {
            drop(Box::from_raw(ptr));
        }
    }));
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_n_points(ptr: *const FoamMesh) -> usize {
    catch_unwind(AssertUnwindSafe(|| {
        if ptr.is_null() { 0 } else { (*ptr).n_points() }
    }))
    .unwrap_or(0)
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_n_cells(ptr: *const FoamMesh) -> usize {
    catch_unwind(AssertUnwindSafe(|| {
        if ptr.is_null() { 0 } else { (*ptr).n_cells }
    }))
    .unwrap_or(0)
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_n_faces(ptr: *const FoamMesh) -> usize {
    catch_unwind(AssertUnwindSafe(|| {
        if ptr.is_null() { 0 } else { (*ptr).n_faces() }
    }))
    .unwrap_or(0)
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_n_internal_faces(ptr: *const FoamMesh) -> usize {
    catch_unwind(AssertUnwindSafe(|| {
        if ptr.is_null() { 0 } else { (*ptr).n_internal_faces() }
    }))
    .unwrap_or(0)
}

// Fixed-width 64-bit getters (AUD-034)
#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_n_points_u64(ptr: *const FoamMesh) -> u64 {
    foam_mesh_get_n_points(ptr) as u64
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_n_cells_u64(ptr: *const FoamMesh) -> u64 {
    foam_mesh_get_n_cells(ptr) as u64
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_n_faces_u64(ptr: *const FoamMesh) -> u64 {
    foam_mesh_get_n_faces(ptr) as u64
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_n_internal_faces_u64(ptr: *const FoamMesh) -> u64 {
    foam_mesh_get_n_internal_faces(ptr) as u64
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_n_face_nodes_u64(ptr: *const FoamMesh) -> u64 {
    catch_unwind(AssertUnwindSafe(|| {
        if ptr.is_null() { 0 } else { (*ptr).face_nodes_i64.len() as u64 }
    }))
    .unwrap_or(0)
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_n_patches_u64(ptr: *const FoamMesh) -> u64 {
    catch_unwind(AssertUnwindSafe(|| {
        if ptr.is_null() { 0 } else { (*ptr).boundaries.len() as u64 }
    }))
    .unwrap_or(0)
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_points_ptr(ptr: *const FoamMesh) -> *const f64 {
    catch_unwind(AssertUnwindSafe(|| {
        if ptr.is_null() || (*ptr).points.is_empty() {
            std::ptr::null()
        } else {
            (*ptr).points.as_ptr() as *const f64
        }
    }))
    .unwrap_or(std::ptr::null())
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_owner_ptr(ptr: *const FoamMesh) -> *const usize {
    catch_unwind(AssertUnwindSafe(|| {
        if ptr.is_null() || (*ptr).owner.is_empty() {
            std::ptr::null()
        } else {
            (*ptr).owner.as_ptr()
        }
    }))
    .unwrap_or(std::ptr::null())
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_neighbour_ptr(ptr: *const FoamMesh) -> *const usize {
    catch_unwind(AssertUnwindSafe(|| {
        if ptr.is_null() || (*ptr).neighbour.is_empty() {
            std::ptr::null()
        } else {
            (*ptr).neighbour.as_ptr()
        }
    }))
    .unwrap_or(std::ptr::null())
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_face_offsets_i64_ptr(ptr: *const FoamMesh) -> *const i64 {
    catch_unwind(AssertUnwindSafe(|| {
        if ptr.is_null() || (*ptr).face_offsets_i64.is_empty() {
            std::ptr::null()
        } else {
            (*ptr).face_offsets_i64.as_ptr()
        }
    }))
    .unwrap_or(std::ptr::null())
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_face_nodes_i64_ptr(ptr: *const FoamMesh) -> *const i64 {
    catch_unwind(AssertUnwindSafe(|| {
        if ptr.is_null() || (*ptr).face_nodes_i64.is_empty() {
            std::ptr::null()
        } else {
            (*ptr).face_nodes_i64.as_ptr()
        }
    }))
    .unwrap_or(std::ptr::null())
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_owner_i64_ptr(ptr: *const FoamMesh) -> *const i64 {
    catch_unwind(AssertUnwindSafe(|| {
        if ptr.is_null() || (*ptr).owner_i64.is_empty() {
            std::ptr::null()
        } else {
            (*ptr).owner_i64.as_ptr()
        }
    }))
    .unwrap_or(std::ptr::null())
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_neighbour_i64_ptr(ptr: *const FoamMesh) -> *const i64 {
    catch_unwind(AssertUnwindSafe(|| {
        if ptr.is_null() || (*ptr).neighbour_i64.is_empty() {
            std::ptr::null()
        } else {
            (*ptr).neighbour_i64.as_ptr()
        }
    }))
    .unwrap_or(std::ptr::null())
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_cell_centres_ptr(ptr: *const FoamMesh) -> *const f64 {
    catch_unwind(AssertUnwindSafe(|| {
        if ptr.is_null() || (*ptr).cell_centres.is_empty() {
            std::ptr::null()
        } else {
            (*ptr).cell_centres.as_ptr() as *const f64
        }
    }))
    .unwrap_or(std::ptr::null())
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_cell_volumes_ptr(ptr: *const FoamMesh) -> *const f64 {
    catch_unwind(AssertUnwindSafe(|| {
        if ptr.is_null() || (*ptr).cell_volumes.is_empty() {
            std::ptr::null()
        } else {
            (*ptr).cell_volumes.as_ptr()
        }
    }))
    .unwrap_or(std::ptr::null())
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_face_areas_ptr(ptr: *const FoamMesh) -> *const f64 {
    catch_unwind(AssertUnwindSafe(|| {
        if ptr.is_null() || (*ptr).face_areas.is_empty() {
            std::ptr::null()
        } else {
            (*ptr).face_areas.as_ptr() as *const f64
        }
    }))
    .unwrap_or(std::ptr::null())
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_face_centres_ptr(ptr: *const FoamMesh) -> *const f64 {
    catch_unwind(AssertUnwindSafe(|| {
        if ptr.is_null() || (*ptr).face_centres.is_empty() {
            std::ptr::null()
        } else {
            (*ptr).face_centres.as_ptr() as *const f64
        }
    }))
    .unwrap_or(std::ptr::null())
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_face_magnitudes_ptr(ptr: *const FoamMesh) -> *const f64 {
    catch_unwind(AssertUnwindSafe(|| {
        if ptr.is_null() || (*ptr).face_magnitudes.is_empty() {
            std::ptr::null()
        } else {
            (*ptr).face_magnitudes.as_ptr()
        }
    }))
    .unwrap_or(std::ptr::null())
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_delta_f_ptr(ptr: *const FoamMesh) -> *const f64 {
    catch_unwind(AssertUnwindSafe(|| {
        if ptr.is_null() || (*ptr).delta_f.is_empty() {
            std::ptr::null()
        } else {
            (*ptr).delta_f.as_ptr() as *const f64
        }
    }))
    .unwrap_or(std::ptr::null())
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_k_f_ptr(ptr: *const FoamMesh) -> *const f64 {
    catch_unwind(AssertUnwindSafe(|| {
        if ptr.is_null() || (*ptr).k_f.is_empty() {
            std::ptr::null()
        } else {
            (*ptr).k_f.as_ptr() as *const f64
        }
    }))
    .unwrap_or(std::ptr::null())
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_total_volume(ptr: *const FoamMesh) -> f64 {
    catch_unwind(AssertUnwindSafe(|| {
        if ptr.is_null() { 0.0 } else { (*ptr).total_volume() }
    }))
    .unwrap_or(f64::NAN)
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_patch_info(
    ptr: *const FoamMesh,
    patch_idx: u64,
    name_buf: *mut c_char,
    name_cap: u64,
    type_buf: *mut c_char,
    type_cap: u64,
    out_start_face: *mut u64,
    out_n_faces: *mut u64,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        if ptr.is_null() {
            return -1;
        }
        let mesh = &*ptr;
        let idx = patch_idx as usize;
        if idx >= mesh.boundaries.len() {
            return -2;
        }
        let p = &mesh.boundaries[idx];
        let write_str = |src: &str, dst: *mut c_char, cap: u64| {
            if dst.is_null() || cap == 0 {
                return;
            }
            let bytes = src.as_bytes();
            let n = bytes.len().min((cap as usize).saturating_sub(1));
            std::ptr::copy_nonoverlapping(bytes.as_ptr(), dst as *mut u8, n);
            *(dst as *mut u8).add(n) = 0;
        };
        write_str(&p.name, name_buf, name_cap);
        write_str(&p.patch_type, type_buf, type_cap);
        if !out_start_face.is_null() {
            *out_start_face = p.start_face as u64;
        }
        if !out_n_faces.is_null() {
            *out_n_faces = p.n_faces as u64;
        }
        0
    }))
    .unwrap_or(-99)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_ffi_null_safety_with_catch_unwind() {
        unsafe {
            assert!(foam_mesh_load(std::ptr::null()).is_null());
            assert!(!foam_last_error().is_null());
            foam_mesh_free(std::ptr::null_mut());
            assert_eq!(foam_mesh_get_n_cells_u64(std::ptr::null()), 0);
            assert_eq!(foam_mesh_get_n_faces_u64(std::ptr::null()), 0);
            assert_eq!(foam_mesh_get_n_patches_u64(std::ptr::null()), 0);
            assert!(foam_mesh_get_face_offsets_i64_ptr(std::ptr::null()).is_null());
            assert_eq!(
                foam_mesh_get_patch_info(
                    std::ptr::null(),
                    0,
                    std::ptr::null_mut(),
                    0,
                    std::ptr::null_mut(),
                    0,
                    std::ptr::null_mut(),
                    std::ptr::null_mut(),
                ),
                -1
            );
        }
    }
}
