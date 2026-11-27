//! C-ABI Structure-of-Arrays (SoA) Data Lake (SUGGESTIONS_BACKEND.md §11.2).

use std::ffi::c_void;

#[repr(u32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CAbiDataType {
    Float64 = 0,
    Float32 = 1,
    Int64 = 2,
    Int32 = 3,
    Uint8 = 4,
}

#[repr(u32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CAbiDeviceId {
    HostCpu = 0,
    GpuCuda = 1,
    GpuVulkan = 2,
}

/// Universal 64-byte aligned C-ABI data lake descriptor.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct CAbiSpan {
    pub ptr: *mut c_void,
    pub len: usize,
    pub dtype: u32,
    pub device_id: u32,
}

impl CAbiSpan {
    #[inline]
    pub fn is_aligned_64(&self) -> bool {
        (self.ptr as usize) % 64 == 0
    }

    #[inline]
    pub fn as_slice<T>(&self) -> &[T] {
        assert!(self.is_aligned_64(), "Buffer pointer is not 64-byte aligned");
        unsafe { std::slice::from_raw_parts(self.ptr as *const T, self.len) }
    }

    #[inline]
    pub fn as_mut_slice<T>(&mut self) -> &mut [T] {
        assert!(self.is_aligned_64(), "Buffer pointer is not 64-byte aligned");
        unsafe { std::slice::from_raw_parts_mut(self.ptr as *mut T, self.len) }
    }
}

/// C-ABI function to verify 64-byte alignment safely under catch_unwind.
#[no_mangle]
pub unsafe extern "C" fn cabi_span_is_aligned(span: *const CAbiSpan) -> bool {
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        if span.is_null() {
            return false;
        }
        (*span).is_aligned_64()
    }))
    .unwrap_or(false)
}
