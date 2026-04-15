// rust_core/src/memory/safemem.rs

use crate::sys::error::KernelError;
use crate::ffi; 
use core::ffi::{c_void, c_char};
use alloc::format;

const KERNEL_SPACE_BASE: u64 = 0xFFFF800000000000;

// Absolute NULL Protection limit
const NULL_GUARD_PAGES: u64 = 0x10000; 

pub struct SafeMem;

impl SafeMem {
    #[inline(always)]
    pub fn is_valid_kernel_region(ptr: *const c_void, len: usize) -> bool {
        let start = ptr as u64;
        let end = start.wrapping_add(len as u64);
        
        // 1. Kesin NULL Pointer Koruması
        if start < NULL_GUARD_PAGES {
            return false;
        }
        if end < start {
            return false; // Taşma (Wrap-around) durumu
        }
        
        // 2. Higher-Half Bölgesi (VMM, KHeap, Kernel Stacks)
        if start >= KERNEL_SPACE_BASE && end >= KERNEL_SPACE_BASE {
            return true;
        }
        
        // 3. Identity Mapped Kernel Area (Code, Data, BSS, Bootstrap Stack)
        if start >= 0x100000 && end < 0x40000000 && end >= start { 
            return true;
        }
        
        false
    }

    pub fn enforce_kernel_region(ptr: *const c_void, len: usize, context: &str) {
        if !Self::is_valid_kernel_region(ptr, len) {
            let msg = format!("SafeMem Violation in {}: Invalid kernel pointer 0x{:X} (Len: {})\0", context, ptr as u64, len);
            unsafe {
                ffi::panic_at(c"safemem.rs".as_ptr() as *const u8, line!() as i32, KernelError::RustSafeMem as u32, msg.as_ptr());
            }
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_safemem_validate(ptr: *const c_void, len: usize, context_ptr: *const c_char) -> bool {
    // OPTİMİZASYON YAMASI: Fast Path! Eğer adres güvenliyse, 
    // yavaş olan CStr çevirisini (strlen) ASLA yapma, anında dön.
    if SafeMem::is_valid_kernel_region(ptr, len) {
        true
    } else {
        // SLOW PATH: Sadece hata durumunda maliyetli string ayrıştırmasını yap.
        unsafe {
            let context = if context_ptr.is_null() {
                "Unknown_C_Module"
            } else {
                core::ffi::CStr::from_ptr(context_ptr).to_str().unwrap_or("Invalid_String") 
            };
            
            ffi::debug_print(&format!("[SAFEMEM] WARNING: Invalid memory access blocked in [{}] at 0x{:X}!\n", context, ptr as u64));
            false
        }
    }
}