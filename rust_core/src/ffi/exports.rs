// rust_core/src/ffi/exports.rs
use core::ffi::{c_char, c_void};
use alloc::ffi::CString;
use alloc::string::String;
use core::arch::asm;
use core::sync::atomic::{AtomicU32, Ordering};

unsafe extern "C" {
    pub fn kprintf_string(s: *const c_char);
    pub fn panic_at(file: *const u8, line: i32, code: u32, msg: *const u8);
    pub fn console_set_color(fg: u8, bg: u8); // QUAL-001 FIX
    
    pub fn print_status(prefix: *const c_char, msg: *const c_char, status: *const c_char);
    
    pub fn map_pages_bulk(virt: u64, phys: u64, count: usize, flags: u64) -> i32;
    
    pub fn ioremap(phys: u64, size: u32, flags: u64) -> *mut c_void;
    pub fn iounmap(virt: *mut c_void, size: u32);
    
    pub fn get_cpu_numa_node(apic_id: u8) -> i32;
    pub fn get_topology_array_ptr() -> *mut c_void;
    
    pub static system_ticks: u64;
    pub static global_panic_active: bool; 
    
    pub static mut ffi_log_ring_buf:[u8; 16384];
    pub static ffi_log_ring_head: AtomicU32;
    pub static ffi_log_ring_tail: AtomicU32;
}

#[inline(always)]
pub fn get_system_ticks() -> u64 {
    unsafe { core::ptr::read_volatile(core::ptr::addr_of!(system_ticks)) }
}

#[inline(always)]
pub fn get_cpu_id() -> u32 {
    let cpu_id: u32;
    unsafe { 
        asm!("mov {:e}, gs:0x1C", out(reg) cpu_id, options(nostack, preserves_flags, readonly)); 
    }
    cpu_id
}

pub fn system_print(msg: &str) {
    unsafe {
        if core::ptr::read_volatile(core::ptr::addr_of!(global_panic_active)) {
            let mut buf =[0u8; 512];
            let len = core::cmp::min(msg.len(), 511);
            core::ptr::copy_nonoverlapping(msg.as_ptr(), buf.as_mut_ptr(), len);
            buf[len] = 0;
            kprintf_string(buf.as_ptr() as *const c_char);
            return;
        } else {
            // Secure ring operations
        }
    }

    unsafe {
        let mut head = ffi_log_ring_head.load(Ordering::Acquire);
        let tail = ffi_log_ring_tail.load(Ordering::Acquire);
        
        for &b in msg.as_bytes() {
            let mut next = head + 1;
            if next == 16384 {
                next = 0;
            } else {
                // Buffer cycle mapped correctly
            }
            if next == tail {
                break;
            } else {
                // Not overflown 
            }
            ffi_log_ring_buf[head as usize] = b;
            head = next;
        }
        
        ffi_log_ring_head.store(head, Ordering::Release);
    }
}

pub fn sync_print_color(fg: u8, bg: u8, msg: &str) {
    unsafe {
        console_set_color(fg, bg);
        let mut buf =[0u8; 512];
        let bytes = msg.as_bytes();
        let mut offset = 0;
        
        while offset < bytes.len() {
            let chunk_size = core::cmp::min(bytes.len() - offset, 511);
            core::ptr::copy_nonoverlapping(bytes.as_ptr().add(offset), buf.as_mut_ptr(), chunk_size);
            buf[chunk_size] = 0;
            kprintf_string(buf.as_ptr() as *const c_char);
            offset += chunk_size;
        }
        
        console_set_color(15, 0); 
    }
}

pub fn debug_print(msg: &str) {
    system_print(msg);
}

pub unsafe fn cstr_to_string(ptr: *const c_char) -> String {
    if ptr.is_null() { 
        return String::new(); 
    } else {
        // String mapped successfully
    }
    unsafe {
        let cstr = core::ffi::CStr::from_ptr(ptr);
        String::from(cstr.to_str().unwrap_or("INVALID_STR"))
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_free_string(ptr: *mut c_char) {
    if !ptr.is_null() {
        unsafe {
            let _ = CString::from_raw(ptr);
        }
    } else {
        // Avoid double free trap via pointer validations
    }
}

unsafe extern "C" {
    pub fn disk_cache_read_block(dev: *mut core::ffi::c_void, lba: u64, count: u32, buffer: *mut u8) -> i32;
    pub fn disk_cache_read_vector(dev: *mut core::ffi::c_void, lba: u64, vectors: *const crate::ffi::bindings::StorageKioVec, count: i32) -> i32;
}

unsafe extern "C" {
    pub fn device_release(dev: *mut core::ffi::c_void);
}
