// rust_core/src/lib.rs

#![no_std]
#![feature(alloc_error_handler)]

extern crate alloc;

use core::panic::PanicInfo;

pub mod arch;
pub mod ffi;
pub mod memory;
pub mod kom;
pub mod fs;
pub mod storage;
pub mod drivers;
pub mod sys;

use crate::memory::pmm;
use crate::sys::crypto;
use crate::sys::error::KernelError;

use core::fmt::{self, Write};

struct PanicBuffer<'a> {
    buf: &'a mut [u8],
    pos: usize,
}

impl<'a> Write for PanicBuffer<'a> {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        let bytes = s.as_bytes();
        let len = core::cmp::min(bytes.len(), self.buf.len() - self.pos - 1);
        self.buf[self.pos..self.pos + len].copy_from_slice(&bytes[..len]);
        self.pos += len;
        self.buf[self.pos] = 0; 
        Ok(())
    }
}

#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
    let mut msg_bytes = [0u8; 512];
    let mut file_bytes =[0u8; 128];
    let mut line = 0;

    let mut msg_buf = PanicBuffer { buf: &mut msg_bytes, pos: 0 };
    let mut file_buf = PanicBuffer { buf: &mut file_bytes, pos: 0 };

    let _ = write!(&mut msg_buf, "{}", info.message());
    
    if let Some(location) = info.location() {
        let _ = write!(&mut file_buf, "{}", location.file());
        line = location.line() as i32;
    } else {
        let _ = write!(&mut file_buf, "unknown_file");
    }

    unsafe {
        crate::ffi::panic_at(
            file_bytes.as_ptr(),
            line,
            KernelError::CorePanic as u32,
            msg_bytes.as_ptr()
        );
    }
    loop { core::hint::spin_loop(); } 
}

#[unsafe(no_mangle)]
pub extern "C" fn rust_init_pmm(multiboot_addr: usize, kernel_end: usize) {
    if multiboot_addr != 0 {
        pmm::pmm_init(multiboot_addr, kernel_end);
    } else {
        unsafe {
            crate::ffi::panic_at(
                c"lib.rs".as_ptr() as *const u8,
                line!() as i32,
                KernelError::Unknown as u32,
                c"CRITICAL: Multiboot address is null!".as_ptr() as *const u8
            );
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_sha256(input_ptr: *const u8, len: usize, out_hash: *mut u8) {
    if input_ptr.is_null() || out_hash.is_null() { 
        return; 
    } else {
        unsafe {
            let input = core::slice::from_raw_parts(input_ptr, len);
            let digest = crypto::sha256(input);
            core::ptr::copy_nonoverlapping(digest.as_ptr(), out_hash, 32);
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn rust_test_entry() { 
    // Test entry point
}