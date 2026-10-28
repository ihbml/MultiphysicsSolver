//! ffi.rs - C-ABI compatibility layer for C++ and Modern Fortran (iso_c_binding).

use crate::mesh::FoamMesh;
use crate::parser::load_poly_mesh;
use std::ffi::CStr;
use std::os::raw::c_char;
use std::path::Path;

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_load(dir_path: *const c_char) -> *mut FoamMesh {
    if dir_path.is_null() {
        return std::ptr::null_mut();
    }
    let c_str = match CStr::from_ptr(dir_path).to_str() {
        Ok(s) => s,
        Err(_) => return std::ptr::null_mut(),
    };
    let path = Path::new(c_str);
    match load_poly_mesh(path) {
        Ok(mesh) => Box::into_raw(Box::new(mesh)),
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_free(ptr: *mut FoamMesh) {
    if !ptr.is_null() {
        drop(Box::from_raw(ptr));
    }
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_n_points(ptr: *const FoamMesh) -> usize {
    if ptr.is_null() { 0 } else { (*ptr).n_points() }
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_n_cells(ptr: *const FoamMesh) -> usize {
    if ptr.is_null() { 0 } else { (*ptr).n_cells }
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_n_faces(ptr: *const FoamMesh) -> usize {
    if ptr.is_null() { 0 } else { (*ptr).n_faces() }
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_n_internal_faces(ptr: *const FoamMesh) -> usize {
    if ptr.is_null() { 0 } else { (*ptr).n_internal_faces() }
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_points_ptr(ptr: *const FoamMesh) -> *const f64 {
    if ptr.is_null() || (*ptr).points.is_empty() {
        std::ptr::null()
    } else {
        (*ptr).points.as_ptr() as *const f64
    }
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_owner_ptr(ptr: *const FoamMesh) -> *const usize {
    if ptr.is_null() || (*ptr).owner.is_empty() {
        std::ptr::null()
    } else {
        (*ptr).owner.as_ptr()
    }
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_neighbour_ptr(ptr: *const FoamMesh) -> *const usize {
    if ptr.is_null() || (*ptr).neighbour.is_empty() {
        std::ptr::null()
    } else {
        (*ptr).neighbour.as_ptr()
    }
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_cell_centres_ptr(ptr: *const FoamMesh) -> *const f64 {
    if ptr.is_null() || (*ptr).cell_centres.is_empty() {
        std::ptr::null()
    } else {
        (*ptr).cell_centres.as_ptr() as *const f64
    }
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_cell_volumes_ptr(ptr: *const FoamMesh) -> *const f64 {
    if ptr.is_null() || (*ptr).cell_volumes.is_empty() {
        std::ptr::null()
    } else {
        (*ptr).cell_volumes.as_ptr()
    }
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_face_areas_ptr(ptr: *const FoamMesh) -> *const f64 {
    if ptr.is_null() || (*ptr).face_areas.is_empty() {
        std::ptr::null()
    } else {
        (*ptr).face_areas.as_ptr() as *const f64
    }
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_face_centres_ptr(ptr: *const FoamMesh) -> *const f64 {
    if ptr.is_null() || (*ptr).face_centres.is_empty() {
        std::ptr::null()
    } else {
        (*ptr).face_centres.as_ptr() as *const f64
    }
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_delta_f_ptr(ptr: *const FoamMesh) -> *const f64 {
    if ptr.is_null() || (*ptr).delta_f.is_empty() {
        std::ptr::null()
    } else {
        (*ptr).delta_f.as_ptr() as *const f64
    }
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_k_f_ptr(ptr: *const FoamMesh) -> *const f64 {
    if ptr.is_null() || (*ptr).k_f.is_empty() {
        std::ptr::null()
    } else {
        (*ptr).k_f.as_ptr() as *const f64
    }
}

#[no_mangle]
pub unsafe extern "C" fn foam_mesh_get_total_volume(ptr: *const FoamMesh) -> f64 {
    if ptr.is_null() { 0.0 } else { (*ptr).total_volume() }
}
