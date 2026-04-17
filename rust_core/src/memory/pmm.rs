// rust_core/src/memory/pmm.rs

use crate::arch::sync::IrqSpinlock;
use core::ffi::c_void;
use core::ptr;
use core::sync::atomic::{AtomicUsize, Ordering};

const PAGE_SIZE:      usize = 4096;
const MAX_NUMA_NODES: usize = 8;
const MAX_CPUS:       usize = 32;
const MAGAZINE_SIZE:  usize = 32;
const MAX_EXTENTS:    usize = 1024;

const REF_FREE:     u8 = 0x00;
const REF_RESERVED: u8 = 0xFF;

unsafe extern "C" {
    fn get_apic_id() -> u8;
    fn panic_at(file: *const u8, line: i32, code: u32, msg: *const u8);
    fn map_pages_bulk(virt: u64, phys: u64, count: usize, flags: u64) -> i32;
    fn kprintf_string(s: *const i8);
}

const PAGE_PRESENT: u64 = 0x01;
const PAGE_WRITE:   u64 = 0x02;
const PAGE_HUGE:    u64 = 0x80;
const PAGE_GLOBAL:  u64 = 0x100;

const KERR_HEAP_DOUBLE_FREE: u32 = 0x23;

#[repr(C, packed)]
struct MultibootTag { typ: u32, size: u32 }

#[repr(C, packed)]
struct MmapEntry { addr: u64, len: u64, typ: u32, zero: u32 }

#[repr(C, packed)]
struct MmapTag { typ: u32, size: u32, entry_size: u32, entry_version: u32 }

#[derive(Clone, Copy, Default)]
struct Extent {
    start: usize,
    count: usize,
}

#[derive(Clone, Copy, Default)]
struct NumaNode {
    active: bool,
    start_frame: usize,
    end_frame: usize,
}

struct PmmState {
    extents: [Extent; MAX_EXTENTS],
    extent_count: usize,

    ref_counts: *mut u8,
    total_frames: usize,
    used_frames: usize,
    reserved_frames: usize,
    hw_reserved_frames: usize,
    total_usable_memory_bytes: usize,

    initialized: bool,
    nodes: [NumaNode; MAX_NUMA_NODES],
}

unsafe impl Send for PmmState {}

static PMM: IrqSpinlock<PmmState> = IrqSpinlock::new(PmmState {
    extents:[Extent { start: 0, count: 0 }; MAX_EXTENTS],
    extent_count: 0,
    ref_counts: ptr::null_mut(),
    total_frames: 0,
    used_frames: 0,
    reserved_frames: 0,
    hw_reserved_frames: 0,
    total_usable_memory_bytes: 0,
    initialized: false,
    nodes:[NumaNode { active: false, start_frame: 0, end_frame: 0 }; MAX_NUMA_NODES],
});

#[derive(Copy, Clone)]
struct Magazine {
    frames: [usize; MAGAZINE_SIZE],
    count: usize,
}

struct PerCpuMagazines { slots: [Magazine; MAX_CPUS] }
unsafe impl Sync for PerCpuMagazines {}

static mut CPU_MAGAZINES: PerCpuMagazines = PerCpuMagazines {
    slots:[Magazine { frames: [0usize; MAGAZINE_SIZE], count: 0 }; MAX_CPUS],
};

static MAGAZINE_TOTAL: AtomicUsize = AtomicUsize::new(0);

#[cold]
#[inline(never)]
unsafe fn pmm_double_free_panic(frame: usize) -> ! {
    let mut buf = [0u8; 80];
    let prefix = b"PMM Double Free! Frame: 0x";
    buf[..prefix.len()].copy_from_slice(prefix);
    let mut pos  = prefix.len();
    let mut hex  = frame * PAGE_SIZE;
    let mut tmp  = [0u8; 16];
    let mut tlen = 0usize;
    loop {
        let nibble = (hex & 0xF) as u8;
        tmp[tlen] = if nibble < 10 { b'0' + nibble } else { b'A' + nibble - 10 };
        tlen += 1;
        hex >>= 4;
        if hex == 0 { break; } else { /* Proceed */ }
    }
    for i in (0..tlen).rev() {
        if pos < buf.len() - 1 { buf[pos] = tmp[i]; pos += 1; } else { /* Stop */ }
    }
    buf[pos] = 0;

    unsafe {
        panic_at(
            c"pmm.rs".as_ptr() as *const u8,
            line!() as i32,
            KERR_HEAP_DOUBLE_FREE,
            buf.as_ptr(),
        );
    }
    loop { core::hint::spin_loop(); }
}

fn free_region_internal(state: &mut PmmState, start: usize, count: usize) {
    if count == 0 { return; } else { /* Proceed */ }

    let mut insert_idx = state.extent_count;
    for i in 0..state.extent_count {
        if state.extents[i].start > start {
            insert_idx = i;
            break;
        } else {
            // Keep looking for insertion point
        }
    }

    if state.extent_count >= MAX_EXTENTS {
        unsafe { 
            panic_at(c"pmm.rs".as_ptr() as *const u8, line!() as i32, 0x21, 
                     c"PMM Extent Array Overflow!".as_ptr() as *const u8); 
        }
    } else {
        for i in (insert_idx..state.extent_count).rev() {
            state.extents[i+1] = state.extents[i];
        }
        state.extents[insert_idx] = Extent { start, count };
        state.extent_count += 1;
    }

    let mut w = 0;
    if state.extent_count > 0 {
        for r in 1..state.extent_count {
            if state.extents[w].start + state.extents[w].count == state.extents[r].start {
                state.extents[w].count += state.extents[r].count;
            } else {
                w += 1;
                state.extents[w] = state.extents[r];
            }
        }
        state.extent_count = w + 1;
    } else {
        // Array empty
    }
}

fn reserve_region_internal(state: &mut PmmState, res_start: usize, res_count: usize) {
    if res_count == 0 { return; } else { /* Proceed */ }
    let res_end = res_start + res_count;
    
    let mut new_extents = [Extent::default(); MAX_EXTENTS];
    let mut new_count = 0;
    
    for i in 0..state.extent_count {
        let ext = state.extents[i];
        let ext_end = ext.start + ext.count;
        
        if res_start < ext_end && res_end > ext.start { 
            if ext.start < res_start {
                if new_count < MAX_EXTENTS {
                    new_extents[new_count] = Extent { start: ext.start, count: res_start - ext.start };
                    new_count += 1;
                } else {
                    // Maximum bounds
                }
            } else {
                // Lower bound clipped
            }
            if ext_end > res_end {
                if new_count < MAX_EXTENTS {
                    new_extents[new_count] = Extent { start: res_end, count: ext_end - res_end };
                    new_count += 1;
                } else {
                    // Maximum bounds
                }
            } else {
                // Upper bound clipped
            }
        } else {
            if new_count < MAX_EXTENTS {
                new_extents[new_count] = ext;
                new_count += 1;
            } else {
                // Maximum bounds
            }
        }
    }
    state.extents = new_extents;
    state.extent_count = new_count;
}

fn alloc_contiguous_internal(state: &mut PmmState, count: usize, align: usize, preferred_node: Option<usize>) -> Option<usize> {
    for i in 0..state.extent_count {
        let ext = state.extents[i];
        let aligned_start = (ext.start + align - 1) & !(align - 1);
        
        if aligned_start >= ext.start {
            // FIX: Removed unused `mut valid_node = true;` logic pattern.
            // Using idiomatic Rust expression binding without overwriting unused states.
            let valid_node = if let Some(node_id) = preferred_node {
                if node_id < MAX_NUMA_NODES {
                    let node = &state.nodes[node_id];
                    if node.active {
                        if aligned_start >= node.start_frame && (aligned_start + count) <= node.end_frame {
                            true
                        } else {
                            false
                        }
                    } else {
                        false
                    }
                } else {
                    false
                }
            } else {
                true // Global unconstrained allocation
            };

            if valid_node {
                let shift = aligned_start - ext.start;
                if ext.count >= shift + count {
                    let rem_start = aligned_start + count;
                    let rem_count = (ext.start + ext.count) - rem_start;
                    
                    for j in i..state.extent_count - 1 {
                        state.extents[j] = state.extents[j+1];
                    }
                    state.extent_count -= 1;
                    
                    if rem_count > 0 { free_region_internal(state, rem_start, rem_count); } else { /* Clean block */ }
                    if shift > 0 { free_region_internal(state, ext.start, shift); } else { /* Perfectly aligned */ }
                    
                    return Some(aligned_start);
                } else {
                    // Contiguous memory too small in this extent
                }
            } else {
                // Skips extent because it doesn't match the required NUMA Node boundary.
            }
        } else {
            // Misaligned bound failure
        }
    }
    None
}

#[unsafe(no_mangle)]
pub extern "C" fn pmm_init(multiboot_addr: usize, kernel_end: usize) {
    let mut state = PMM.lock();
    if state.initialized { return; } else { /* First time configuration */ }

    let mut max_ram_addr = 0u64;
    let mut usable_ram   = 0u64;

    unsafe {
        let mut ptr = (multiboot_addr + 8) as *const u8;
        loop {
            let tag = ptr::read_unaligned(ptr as *const MultibootTag);
            if tag.typ == 0 { break; } else { /* Valid tag */ }
            if tag.typ == 6 {
                let mmap = ptr::read_unaligned(ptr as *const MmapTag);
                let mut eptr = ptr.add(core::mem::size_of::<MmapTag>());
                let end_ptr  = ptr.add(tag.size as usize);
                while eptr < end_ptr {
                    let entry = ptr::read_unaligned(eptr as *const MmapEntry);
                    let region_end = entry.addr + entry.len;
                    if region_end > max_ram_addr { max_ram_addr = region_end; } else { /* Retain highest */ }
                    if entry.typ == 1 { 
                        usable_ram += entry.len; 
                        free_region_internal(&mut state, (entry.addr as usize) / PAGE_SIZE, (entry.len as usize) / PAGE_SIZE);
                    } else {
                        // Hardware reserved memory region
                    }
                    eptr = eptr.add(mmap.entry_size as usize);
                }
            } else {
                // Not Mmap Tag
            }
            ptr = ptr.add(((tag.size + 7) & !7) as usize);
        }
    }

    state.total_frames = (max_ram_addr as usize) / PAGE_SIZE;
    state.total_usable_memory_bytes = usable_ram as usize;

    let mb_size = unsafe { *(multiboot_addr as *const u32) } as usize;
    let mb_end = multiboot_addr + mb_size;
    let safe_after = if mb_end > kernel_end { mb_end } else { kernel_end };
    
    let ref_start = (safe_after + PAGE_SIZE - 1) & !(PAGE_SIZE - 1);
    let ref_bytes = state.total_frames;
    state.ref_counts = ref_start as *mut u8;

    reserve_region_internal(&mut state, 0, 0x100000 / PAGE_SIZE);
    reserve_region_internal(&mut state, 0x100000 / PAGE_SIZE, (kernel_end - 0x100000).div_ceil(PAGE_SIZE));
    reserve_region_internal(&mut state, multiboot_addr / PAGE_SIZE, mb_size.div_ceil(PAGE_SIZE));
    reserve_region_internal(&mut state, ref_start / PAGE_SIZE, ref_bytes.div_ceil(PAGE_SIZE));

    unsafe { ptr::write_bytes(state.ref_counts, REF_RESERVED, state.total_frames); }

    let mut free_frames = 0;
    for i in 0..state.extent_count {
        let ext = state.extents[i];
        free_frames += ext.count;
        for f in ext.start .. (ext.start + ext.count) {
            unsafe { *state.ref_counts.add(f) = REF_FREE; }
        }
    }

    state.used_frames = state.total_frames - free_frames;
    state.hw_reserved_frames = state.total_frames.saturating_sub(usable_ram as usize / PAGE_SIZE);
    state.reserved_frames = state.used_frames.saturating_sub(state.hw_reserved_frames);

    state.initialized = true;
}

unsafe fn alloc_from_node(state: &mut PmmState, preferred_node: usize) -> *mut c_void {
    let mut order = [0usize; MAX_NUMA_NODES];
    order[0] = preferred_node;
    let mut k = 1;
    for i in 0..MAX_NUMA_NODES {
        if i != preferred_node { order[k] = i; k += 1; } else { /* Placed first */ }
    }

    for &node_id in order.iter() {
        if node_id >= MAX_NUMA_NODES || !state.nodes[node_id].active { continue; } else { /* Valid Node */ }
        if let Some(f) = alloc_contiguous_internal(state, 1, 1, Some(node_id)) {
            unsafe { *state.ref_counts.add(f) = 1; }
            state.used_frames += 1;
            return (f * PAGE_SIZE) as *mut c_void;
        } else {
            // Node empty, falling back to next available NUMA node.
        }
    }
    
    // Total System Starvation Fallback (Ignore NUMA completely)
    if let Some(f) = alloc_contiguous_internal(state, 1, 1, None) {
        unsafe { *state.ref_counts.add(f) = 1; }
        state.used_frames += 1;
        return (f * PAGE_SIZE) as *mut c_void;
    } else {
        ptr::null_mut()
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pmm_alloc_frame() -> *mut c_void {
    let cpu = unsafe { get_apic_id() } as usize;
    if cpu < MAX_CPUS {
        let mag = unsafe { &mut CPU_MAGAZINES.slots[cpu] };
        if mag.count > 0 {
            mag.count -= 1;
            MAGAZINE_TOTAL.fetch_sub(1, Ordering::Relaxed);
            return (mag.frames[mag.count] * PAGE_SIZE) as *mut c_void;
        } else {
            if unsafe { magazine_refill(cpu) } > 0 {
                let mag2 = unsafe { &mut CPU_MAGAZINES.slots[cpu] };
                mag2.count -= 1;
                MAGAZINE_TOTAL.fetch_sub(1, Ordering::Relaxed);
                return (mag2.frames[mag2.count] * PAGE_SIZE) as *mut c_void;
            } else {
                // Magazine totally empty, fallback to Global PMM
            }
        }
    } else {
        // CPU limit breach
    }

    let mut state = PMM.lock();
    if !state.initialized { return ptr::null_mut(); } else {
        unsafe { alloc_from_node(&mut state, 0) }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pmm_free_frame(ptr: *mut c_void) {
    if ptr.is_null() { return; } else { /* Active */ }

    let frame = ptr as usize / PAGE_SIZE;
    let cpu = unsafe { get_apic_id() } as usize;
    
    if cpu < MAX_CPUS {
        let mag = unsafe { &mut CPU_MAGAZINES.slots[cpu] };
        if mag.count < MAGAZINE_SIZE {
            mag.frames[mag.count] = frame;
            mag.count += 1;
            MAGAZINE_TOTAL.fetch_add(1, Ordering::Relaxed);
            return;
        } else {
            unsafe { magazine_flush(cpu); }
            let mag2 = unsafe { &mut CPU_MAGAZINES.slots[cpu] };
            mag2.frames[0] = frame;
            mag2.count     = 1;
            MAGAZINE_TOTAL.fetch_add(1, Ordering::Relaxed);
            return;
        }
    } else {
        // Safe bound bypassed
    }

    let mut state = PMM.lock();
    unsafe { free_frame_locked(&mut state, ptr); }
}

unsafe fn free_frame_locked(state: &mut PmmState, ptr: *mut c_void) {
    if ptr.is_null() { return; } else { /* Valid */ }
    let frame = ptr as usize / PAGE_SIZE;
    if frame >= state.total_frames { return; } else { /* Valid */ }

    unsafe {
        let ref_ptr = state.ref_counts.add(frame);
        let refs = *ref_ptr;

        if refs == REF_FREE || refs == REF_RESERVED {
            pmm_double_free_panic(frame);
        } else {
            *ref_ptr = refs - 1;
            if *ref_ptr == REF_FREE {
                free_region_internal(state, frame, 1);
                state.used_frames -= 1;
            } else {
                // Shared ownership maintained
            }
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pmm_inc_ref(ptr: *mut c_void) {
    if ptr.is_null() { return; } else { /* Valid */ }
    let state = PMM.lock();
    if !state.initialized { return; } else { /* Valid */ }
    let frame = ptr as usize / PAGE_SIZE;
    if frame >= state.total_frames { return; } else {
        unsafe {
            let r = state.ref_counts.add(frame);
            if *r == REF_RESERVED { return; } else { /* Valid */ }
            if *r < 254 { *r += 1; } else { /* Max bounds */ }
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pmm_dec_ref(ptr: *mut c_void) {
    if ptr.is_null() { return; } else { /* Valid */ }
    let frame = ptr as usize / PAGE_SIZE;
    let mut state = PMM.lock();
    if !state.initialized { return; } else { /* Valid */ }
    if frame >= state.total_frames { return; } else {
        unsafe {
            let ref_ptr = state.ref_counts.add(frame);
            let refs = *ref_ptr;
            if refs == REF_RESERVED { return; } else { /* Valid */ }
            if refs == REF_FREE { pmm_double_free_panic(frame); } else {
                *ref_ptr = refs - 1;
                if *ref_ptr == REF_FREE {
                    free_region_internal(&mut state, frame, 1);
                    state.used_frames -= 1;
                } else {
                    // Shared block
                }
            }
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pmm_get_ref(ptr: *mut c_void) -> u8 {
    let state = PMM.lock();
    if !state.initialized || ptr.is_null() { return 0; } else {
        let frame = ptr as usize / PAGE_SIZE;
        if frame >= state.total_frames { return 0; } else {
            unsafe { *state.ref_counts.add(frame) }
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pmm_alloc_contiguous(count: usize) -> *mut c_void {
    unsafe { pmm_alloc_contiguous_aligned(count, 1) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pmm_alloc_contiguous_aligned(count: usize, align_pages: usize) -> *mut c_void {
    if count == 0 { return ptr::null_mut(); } else { /* Safe */ }
    let align = if align_pages == 0 { 1 } else { align_pages };
    let mut state = PMM.lock(); 
    if !state.initialized { return ptr::null_mut(); } else {
        if let Some(f) = alloc_contiguous_internal(&mut *state, count, align, None) {
            for i in 0..count {
                unsafe { *state.ref_counts.add(f + i) = 1; }
            }
            state.used_frames += count;
            return (f * PAGE_SIZE) as *mut c_void;
        } else {
            ptr::null_mut()
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pmm_free_contiguous(ptr: *mut c_void, count: usize) {
    if ptr.is_null() || count == 0 { return; } else { /* Valid */ }
    let start_frame = ptr as usize / PAGE_SIZE;
    let mut state = PMM.lock();
    if !state.initialized { return; } else {
        if start_frame >= state.total_frames { return; } else {
            let mut free_start = 0;
            let mut free_count = 0;
            for i in 0..count {
                let frame = start_frame + i;
                if frame >= state.total_frames { break; } else {
                    unsafe {
                        let ref_ptr = state.ref_counts.add(frame);
                        let refs = *ref_ptr;
                        if refs == REF_FREE || refs == REF_RESERVED { pmm_double_free_panic(frame); } else {
                            *ref_ptr = refs - 1;
                            if *ref_ptr == REF_FREE {
                                if free_count == 0 {
                                    free_start = frame; free_count = 1;
                                } else if free_start + free_count == frame {
                                    free_count += 1;
                                } else {
                                    free_region_internal(&mut state, free_start, free_count);
                                    free_start = frame; free_count = 1;
                                }
                                state.used_frames -= 1;
                            } else {
                                // Maintained Ref
                            }
                        }
                    }
                }
            }
            if free_count > 0 { free_region_internal(&mut state, free_start, free_count); } else { /* Safe */ }
        }
    }
}

unsafe fn magazine_refill(cpu: usize) -> usize {
    if cpu >= MAX_CPUS { return 0; } else {
        let mut state = PMM.lock();
        if !state.initialized { return 0; } else {
            let mag = unsafe { &mut CPU_MAGAZINES.slots[cpu] };
            let mut loaded = 0usize;
            if let Some(start) = alloc_contiguous_internal(&mut state, MAGAZINE_SIZE, 1, None) {
                for i in 0..MAGAZINE_SIZE {
                    mag.frames[i] = start + i;
                    unsafe { *state.ref_counts.add(start + i) = 1; }
                }
                loaded = MAGAZINE_SIZE;
            } else {
                while loaded < MAGAZINE_SIZE {
                    if let Some(f) = alloc_contiguous_internal(&mut state, 1, 1, None) {
                        mag.frames[loaded] = f;
                        unsafe { *state.ref_counts.add(f) = 1; }
                        loaded += 1;
                    } else { break; }
                }
            }
            mag.count = loaded;
            state.used_frames += loaded;
            MAGAZINE_TOTAL.fetch_add(loaded, Ordering::Relaxed);
            loaded
        }
    }
}

unsafe fn magazine_flush(cpu: usize) {
    if cpu >= MAX_CPUS { return; } else {
        let mag = unsafe { &mut CPU_MAGAZINES.slots[cpu] };
        if mag.count == 0 { return; } else {
            let flush_count = mag.count;
            let mut state = PMM.lock();
            for i in 0..flush_count {
                let real_ptr = (mag.frames[i] * PAGE_SIZE) as *mut c_void;
                unsafe { free_frame_locked(&mut state, real_ptr); }
            }
            mag.count = 0;
            MAGAZINE_TOTAL.fetch_sub(flush_count, Ordering::Relaxed);
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn pmm_get_total_memory() -> u64 {
    PMM.lock().total_usable_memory_bytes as u64
}

#[unsafe(no_mangle)]
pub extern "C" fn pmm_get_used_memory() -> u64 {
    let state = PMM.lock();
    let hw_rsrv = state.hw_reserved_frames;
    let used = state.used_frames;
    let mag_count = MAGAZINE_TOTAL.load(Ordering::Relaxed);
    
    // GÜVENLİK YAMASI (BUG FIX): Magazindeki boş sayfalar kullanımdan düşülür (Saturating Sub).
    let effective = used.saturating_sub(mag_count);
    
    if effective > hw_rsrv { ((effective - hw_rsrv) * PAGE_SIZE) as u64 } else { 0 }
}

#[unsafe(no_mangle)]
pub extern "C" fn pmm_get_free_memory() -> u64 {
    pmm_get_total_memory().saturating_sub(pmm_get_used_memory())
}

#[unsafe(no_mangle)]
pub extern "C" fn pmm_get_reserved_memory() -> u64 {
    (PMM.lock().reserved_frames * PAGE_SIZE) as u64
}

#[unsafe(no_mangle)]
pub extern "C" fn pmm_flush_magazines() {
    let cpu = unsafe { get_apic_id() } as usize;
    unsafe { magazine_flush(cpu); }
}

#[unsafe(no_mangle)]
pub extern "C" fn pmm_map_remaining_memory() {
    let state = PMM.lock();
    let total_ram = state.total_usable_memory_bytes as u64;
    let mapped_limit: u64 = 4 * 1024 * 1024 * 1024;
    if total_ram <= mapped_limit { return; } else {
        let start = mapped_limit;
        let size = total_ram - start;
        let pages_2mb = size.div_ceil(0x20_0000);
        unsafe {
            map_pages_bulk(start, start, pages_2mb as usize, PAGE_PRESENT | PAGE_WRITE | PAGE_HUGE | PAGE_GLOBAL);
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn pmm_register_region(node_id: u32, base: u64, length: u64) {
    let mut state = PMM.lock();
    let idx = node_id as usize;
    if idx >= MAX_NUMA_NODES { return; } else {
        let sf = (base as usize) / PAGE_SIZE;
        let ef = (base as usize + length as usize) / PAGE_SIZE;
        if state.nodes[idx].active {
            if sf < state.nodes[idx].start_frame { state.nodes[idx].start_frame = sf; } else { /* Bounds mapped */ }
            if ef > state.nodes[idx].end_frame { state.nodes[idx].end_frame = ef; } else { /* Bounds mapped */ }
        } else {
            state.nodes[idx] = NumaNode { active: true, start_frame: sf, end_frame: ef };
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn pmm_print_stats() {
    let total = pmm_get_total_memory();
    let used = pmm_get_used_memory();
    let msg = alloc::format!("[PMM] {} MB used / {} MB total\n\0", used / 1048576, total / 1048576);
    unsafe { kprintf_string(msg.as_ptr() as *const i8); }
}

#[unsafe(no_mangle)]
pub extern "C" fn pmm_is_low_memory() -> i32 {
    let total = pmm_get_total_memory();
    if total == 0 { return 0; } else {
        let used = pmm_get_used_memory();
        if (used * 100 / total) > 90 { 1 } else { 0 }
    }
}