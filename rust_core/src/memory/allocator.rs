// rust_core/src/memory/allocator.rs

use core::alloc::{GlobalAlloc, Layout};
use core::ffi::c_void;
use crate::sys::error::KernelError;

unsafe extern "C" {
    fn kmalloc(size: usize) -> *mut c_void;
    fn kfree(ptr: *mut c_void);
    fn kmalloc_aligned(size: usize, alignment: usize) -> *mut c_void;
    fn kfree_aligned(ptr: *mut c_void);
    fn panic_at(file: *const u8, line: i32, code: u32, msg: *const u8);
}

pub struct KernelAllocator;

unsafe impl GlobalAlloc for KernelAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        unsafe {
            if layout.size() > 0x10000000 {
                panic_at(
                    c"allocator.rs".as_ptr() as *const u8, line!() as i32,
                    KernelError::RustOOM as u32,
                    c"FATAL: Ridiculous Allocation Size Blocked!".as_ptr() as *const u8,
                );
            }
            let ptr = if layout.align() <= 16 {
                kmalloc(layout.size())
            } else {
                kmalloc_aligned(layout.size(), layout.align())
            };
            if ptr.is_null() { core::ptr::null_mut() } else { ptr as *mut u8 }
        }
    }

    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        if ptr.is_null() { return; }
        unsafe {
            if layout.align() <= 16 {
                kfree(ptr as *mut c_void);
            } else {
                kfree_aligned(ptr as *mut c_void);
            }
        }
    }

    unsafe fn realloc(&self, ptr: *mut u8, layout: Layout, new_size: usize) -> *mut u8 {
        unsafe {
            if new_size == 0 {
                if layout.align() <= 16 { kfree(ptr as *mut c_void); }
                else                    { kfree_aligned(ptr as *mut c_void); }
                return core::ptr::null_mut();
            }
            if new_size > 0x10000000 || new_size == 0 { 
                return core::ptr::null_mut();
            }
            let new_ptr = if layout.align() <= 16 {
                kmalloc(new_size)
            } else {
                kmalloc_aligned(new_size, layout.align())
            };
            if new_ptr.is_null() {
                return core::ptr::null_mut();
            }
            let copy_size = core::cmp::min(layout.size(), new_size);
            if copy_size > 0 {
                core::ptr::copy_nonoverlapping(ptr, new_ptr as *mut u8, copy_size);
            }
            if layout.align() <= 16 {
                kfree(ptr as *mut c_void);
            } else {
                kfree_aligned(ptr as *mut c_void);
            }
            new_ptr as *mut u8
        }
    }
}

#[global_allocator]
pub static ALLOCATOR: KernelAllocator = KernelAllocator;

#[alloc_error_handler]
fn alloc_error_handler(_layout: Layout) -> ! {
    unsafe {
        crate::ffi::exports::kprintf_string(c"[MEM] FATAL: Out Of Memory (OOM) hit in safe core space!\n".as_ptr());
        panic_at(
            c"allocator.rs".as_ptr() as *const u8, 0,
            KernelError::RustOOM as u32,
            c"FATAL OOM: Kernel Heap Exhausted (Allocation Failed)!".as_ptr() as *const u8,
        );
    }
    loop { core::hint::spin_loop(); }
}