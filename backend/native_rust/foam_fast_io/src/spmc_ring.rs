//! spmc_ring.rs - Lock-free CFDFLD03 Triple-Buffer SPMC Ring for In-Situ Streaming.
//!
//! Enables a single native solver thread to publish scalar/field snapshots at 60-120+ FPS
//! while multiple IPC/visualization consumers read consistent snapshots with zero locking
//! and zero torn reads via per-slot seqlock + active reader reference counting.

use std::ffi::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicU32, AtomicU64, AtomicUsize, Ordering};

pub const CFDFLD03_MAGIC: &[u8; 8] = b"CFDFLD03";

#[repr(C)]
struct RingSlot {
    seq: AtomicU64,
    readers: AtomicU32,
    step_id: u64,
    sim_time: f64,
    len: usize,
    data: Vec<f64>,
}

impl RingSlot {
    fn new(capacity: usize) -> Self {
        Self {
            seq: AtomicU64::new(0),
            readers: AtomicU32::new(0),
            step_id: 0,
            sim_time: 0.0,
            len: 0,
            data: vec![0.0; capacity],
        }
    }
}

/// Lock-free triple-buffer SPMC ring for in-situ CFD field streaming (`CFDFLD03`).
pub struct CfdFieldRing {
    pub magic: [u8; 8],
    pub capacity: usize,
    slots: [RingSlot; 3],
    latest_slot: AtomicUsize,
    has_published: AtomicU64,
}

impl CfdFieldRing {
    pub fn new(capacity: usize) -> Self {
        Self {
            magic: *CFDFLD03_MAGIC,
            capacity,
            slots: [
                RingSlot::new(capacity),
                RingSlot::new(capacity),
                RingSlot::new(capacity),
            ],
            latest_slot: AtomicUsize::new(0),
            has_published: AtomicU64::new(0),
        }
    }

    /// Publish a field snapshot into a free slot and atomically advance `latest_slot`.
    pub fn publish(&mut self, step_id: u64, sim_time: f64, values: &[f64]) -> Result<usize, String> {
        if values.len() > self.capacity {
            return Err(format!(
                "Snapshot length {} exceeds ring capacity {}",
                values.len(),
                self.capacity
            ));
        }

        let current_latest = self.latest_slot.load(Ordering::Acquire);
        let published = self.has_published.load(Ordering::Acquire) > 0;

        // Prefer a slot that has zero active readers AND is not the currently advertised latest slot.
        let mut chosen: Option<usize> = None;
        for spin in 0..10_000 {
            for idx in 0..3 {
                if published && idx == current_latest {
                    continue;
                }
                if self.slots[idx].readers.load(Ordering::Acquire) == 0 {
                    chosen = Some(idx);
                    break;
                }
            }
            if chosen.is_some() {
                break;
            }
            if spin > 100 {
                std::thread::yield_now();
            }
        }
        let slot_idx = match chosen {
            Some(idx) => idx,
            None => {
                // Fallback: any slot with 0 readers
                (0..3)
                    .find(|&idx| self.slots[idx].readers.load(Ordering::Acquire) == 0)
                    .ok_or_else(|| "All 3 ring slots currently pinned by readers".to_string())?
            }
        };

        let slot = &mut self.slots[slot_idx];
        let prev_seq = slot.seq.load(Ordering::Acquire);
        let write_seq = if prev_seq % 2 == 0 { prev_seq + 1 } else { prev_seq };
        slot.seq.store(write_seq, Ordering::Release);
        std::sync::atomic::fence(Ordering::SeqCst);

        slot.step_id = step_id;
        slot.sim_time = sim_time;
        slot.len = values.len();
        slot.data[..values.len()].copy_from_slice(values);

        std::sync::atomic::fence(Ordering::SeqCst);
        slot.seq.store(write_seq + 1, Ordering::Release);
        self.latest_slot.store(slot_idx, Ordering::Release);
        self.has_published.store(1, Ordering::Release);

        Ok(slot_idx)
    }

    /// Read the most recently published consistent snapshot into `out`.
    pub fn read_latest(&self, out: &mut Vec<f64>) -> Option<(u64, f64)> {
        if self.has_published.load(Ordering::Acquire) == 0 {
            return None;
        }

        for _attempt in 0..64 {
            let idx = self.latest_slot.load(Ordering::Acquire);
            if idx >= 3 {
                continue;
            }
            let slot = &self.slots[idx];
            slot.readers.fetch_add(1, Ordering::AcqRel);
            std::sync::atomic::fence(Ordering::SeqCst);

            let seq1 = slot.seq.load(Ordering::Acquire);
            if seq1 % 2 != 0 || seq1 == 0 {
                slot.readers.fetch_sub(1, Ordering::Release);
                std::hint::spin_loop();
                continue;
            }

            let step_id = slot.step_id;
            let sim_time = slot.sim_time;
            let len = slot.len.min(self.capacity);
            out.clear();
            out.extend_from_slice(&slot.data[..len]);

            std::sync::atomic::fence(Ordering::SeqCst);
            let seq2 = slot.seq.load(Ordering::Acquire);
            slot.readers.fetch_sub(1, Ordering::Release);

            if seq1 == seq2 {
                return Some((step_id, sim_time));
            }
        }
        None
    }
}

#[no_mangle]
pub unsafe extern "C" fn cfdfld03_ring_create(max_values: u64) -> *mut c_void {
    catch_unwind(AssertUnwindSafe(|| {
        let cap = usize::try_from(max_values).unwrap_or(0);
        if cap == 0 || cap > 256_000_000 {
            return std::ptr::null_mut();
        }
        Box::into_raw(Box::new(CfdFieldRing::new(cap))) as *mut c_void
    }))
    .unwrap_or(std::ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn cfdfld03_ring_free(ptr: *mut c_void) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if !ptr.is_null() {
            drop(Box::from_raw(ptr as *mut CfdFieldRing));
        }
    }));
}

#[no_mangle]
pub unsafe extern "C" fn cfdfld03_ring_publish_f64(
    ptr: *mut c_void,
    step_id: u64,
    sim_time: f64,
    data: *const f64,
    len: u64,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        if ptr.is_null() || data.is_null() {
            return -1;
        }
        let n = match usize::try_from(len) {
            Ok(v) => v,
            Err(_) => return -2,
        };
        let ring = &mut *(ptr as *mut CfdFieldRing);
        let slice = std::slice::from_raw_parts(data, n);
        match ring.publish(step_id, sim_time, slice) {
            Ok(_) => 0,
            Err(_) => -3,
        }
    }))
    .unwrap_or(-99)
}

#[no_mangle]
pub unsafe extern "C" fn cfdfld03_ring_read_latest_f64(
    ptr: *const c_void,
    out_step_id: *mut u64,
    out_sim_time: *mut f64,
    out_data: *mut f64,
    cap: u64,
    out_len: *mut u64,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        if ptr.is_null() || out_data.is_null() || out_len.is_null() {
            return -1;
        }
        let ring = &*(ptr as *const CfdFieldRing);
        let mut buf = Vec::new();
        match ring.read_latest(&mut buf) {
            None => 1, // No published snapshot yet
            Some((step_id, sim_time)) => {
                if (buf.len() as u64) > cap {
                    *out_len = buf.len() as u64;
                    return -4;
                }
                std::ptr::copy_nonoverlapping(buf.as_ptr(), out_data, buf.len());
                *out_len = buf.len() as u64;
                if !out_step_id.is_null() {
                    *out_step_id = step_id;
                }
                if !out_sim_time.is_null() {
                    *out_sim_time = sim_time;
                }
                0
            }
        }
    }))
    .unwrap_or(-99)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::{Arc, Mutex};

    #[test]
    fn test_spmc_ring_publish_and_read_latest() {
        let mut ring = CfdFieldRing::new(8);
        let mut out = Vec::new();
        assert!(ring.read_latest(&mut out).is_none());

        ring.publish(1, 0.25, &[1.0, 2.0, 3.0]).unwrap();
        let (step, t) = ring.read_latest(&mut out).expect("snapshot");
        assert_eq!(step, 1);
        assert!((t - 0.25).abs() < 1e-12);
        assert_eq!(out, vec![1.0, 2.0, 3.0]);

        ring.publish(2, 0.5, &[4.0, 5.0, 6.0, 7.0]).unwrap();
        let (step2, t2) = ring.read_latest(&mut out).expect("snapshot 2");
        assert_eq!(step2, 2);
        assert!((t2 - 0.5).abs() < 1e-12);
        assert_eq!(out, vec![4.0, 5.0, 6.0, 7.0]);
    }

    #[test]
    fn test_spmc_ring_concurrent_zero_tear() {
        let ring = Arc::new(Mutex::new(())); // dummy; test C-ABI raw pointer across reader threads
        drop(ring);
        let ring_ptr = unsafe { cfdfld03_ring_create(16) };
        assert!(!ring_ptr.is_null());
        let addr = ring_ptr as usize;

        let stop = Arc::new(std::sync::atomic::AtomicBool::new(false));
        let mut readers = Vec::new();
        for _ in 0..3 {
            let stop_c = Arc::clone(&stop);
            readers.push(std::thread::spawn(move || {
                let ptr = addr as *const CfdFieldRing;
                let mut tmp = Vec::with_capacity(16);
                let mut valid_reads = 0usize;
                while !stop_c.load(Ordering::Acquire) {
                    if let Some((step, _t)) = unsafe { &*ptr }.read_latest(&mut tmp) {
                        assert_eq!(tmp.len(), 16);
                        let expected = step as f64;
                        assert!(tmp.iter().all(|&v| v == expected), "Torn read detected!");
                        valid_reads += 1;
                    }
                }
                valid_reads
            }));
        }

        for step in 1u64..=200 {
            let vals = [step as f64; 16];
            let rc = unsafe { cfdfld03_ring_publish_f64(ring_ptr, step, step as f64 * 0.01, vals.as_ptr(), 16) };
            assert_eq!(rc, 0);
        }
        stop.store(true, Ordering::Release);
        for h in readers {
            let _ = h.join().unwrap();
        }
        unsafe { cfdfld03_ring_free(ring_ptr) };
    }
}
