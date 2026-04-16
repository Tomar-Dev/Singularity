// rust_core/src/storage/dma/sg_builder.rs

use crate::ffi::bindings::{StorageKioVec, StorageDmaChain};
use crate::memory::safemem::SafeMem;
use crate::ffi::{ioremap, iounmap, kprintf_string};
use crate::memory::pmm::{pmm_alloc_contiguous, pmm_free_contiguous, pmm_inc_ref, pmm_dec_ref};
use alloc::format;
use core::ptr;

const PAGE_SIZE: usize = 4096;

#[unsafe(no_mangle)]
pub extern "C" fn rust_dma_guard_validate(
    vectors: *const StorageKioVec, 
    count: usize, 
    _caller_pid: u64
) -> bool {
    if vectors.is_null() || count == 0 { 
        unsafe { kprintf_string(c"Blocked: Vector Array is Null or Empty!\n".as_ptr()); }
        return false; 
    }
    
    let vecs = unsafe { core::slice::from_raw_parts(vectors, count) };
    
    for vec in vecs {
        if vec.size == 0 || vec.virt_addr.is_null() {
            let msg = format!("Blocked: Invalid Vector (Size: {}, VirtAddr: 0x{:X})\n\0", vec.size, vec.virt_addr as u64);
            unsafe { kprintf_string(msg.as_ptr() as *const i8); }
            return false;
        }
        
        if !SafeMem::is_valid_kernel_region(vec.virt_addr, vec.size) {
            let msg = format!("Security Violation: Unsafe memory target 0x{:X}\n\0", vec.virt_addr as u64);
            unsafe { kprintf_string(msg.as_ptr() as *const i8); }
            return false;
        }
    }
    true
}

#[unsafe(no_mangle)]
pub extern "C" fn rust_storage_build_sg_list(
    vectors: *const StorageKioVec, 
    count: usize
) -> StorageDmaChain {
    let mut chain = StorageDmaChain {
        prp1: 0,
        prp2: 0,
        prp_list_virt: ptr::null_mut(),
        total_bytes: 0,
        is_valid: false,
    };
    
    if vectors.is_null() || count == 0 { return chain; }
    let vecs = unsafe { core::slice::from_raw_parts(vectors, count) };
    
    let mut total_size = 0;
    let mut is_aligned = true;
    
    for (i, vec) in vecs.iter().enumerate() {
        total_size += vec.size;
        if i > 0 {
            if (vec.phys_addr & 0xFFF) != 0 || (vec.size % PAGE_SIZE) != 0 {
                is_aligned = false;
            }
        }
        unsafe { pmm_inc_ref(vec.phys_addr as *mut core::ffi::c_void); }
    }
    
    chain.total_bytes = total_size as u32;
    
    if !is_aligned {
        for vec in vecs.iter() {
            unsafe { pmm_dec_ref(vec.phys_addr as *mut core::ffi::c_void); }
        }
        return chain;
    }
    
    chain.prp1 = vecs[0].phys_addr;
    
    if count == 1 && vecs[0].size <= PAGE_SIZE {
        chain.is_valid = true;
        return chain;
    }
    
    if count == 2 && (vecs[0].size + vecs[1].size) <= PAGE_SIZE * 2 {
        chain.prp2 = vecs[1].phys_addr;
        chain.is_valid = true;
        return chain;
    }
    
    let prp_list_pages = (count + 511 - 1) / 511;
    let prp_block_phys = unsafe { pmm_alloc_contiguous(prp_list_pages) as u64 };
    if prp_block_phys == 0 { 
        for vec in vecs.iter() { unsafe { pmm_dec_ref(vec.phys_addr as *mut core::ffi::c_void); } }
        return chain; 
    }
    
    let prp_block_virt = unsafe { ioremap(prp_block_phys, (prp_list_pages * PAGE_SIZE) as u32, 0x1B) };
    if prp_block_virt.is_null() { 
        for vec in vecs.iter() { unsafe { pmm_dec_ref(vec.phys_addr as *mut core::ffi::c_void); } }
        return chain; 
    }
    
    unsafe { ptr::write_bytes(prp_block_virt as *mut u8, 0, prp_list_pages * PAGE_SIZE); }
    
    chain.prp2 = prp_block_phys;
    chain.prp_list_virt = prp_block_virt;
    
    let mut current_prp_list = prp_block_virt as *mut u64;
    let mut prp_idx = 0;
    let mut list_page_idx = 0;
    
    for vec in vecs.iter().skip(1) {
        if prp_idx == 511 {
            list_page_idx += 1;
            let next_page_virt = (prp_block_virt as u64 + (list_page_idx as u64 * PAGE_SIZE as u64)) as *mut u64;
            let next_page_phys = prp_block_phys + (list_page_idx as u64 * PAGE_SIZE as u64);
            unsafe { *current_prp_list.add(511) = next_page_phys; }
            current_prp_list = next_page_virt as *mut u64;
            prp_idx = 0;
        }
        unsafe { *current_prp_list.add(prp_idx) = vec.phys_addr; }
        prp_idx += 1;
    }
    
    chain.is_valid = true;
    chain
}

#[unsafe(no_mangle)]
pub extern "C" fn rust_storage_free_sg_list(chain: *mut StorageDmaChain, vectors: *const StorageKioVec, count: usize) {
    if chain.is_null() || vectors.is_null() || count == 0 { return; }
    
    let vecs = unsafe { core::slice::from_raw_parts(vectors, count) };
    for vec in vecs.iter() {
        unsafe { pmm_dec_ref(vec.phys_addr as *mut core::ffi::c_void); }
    }
    
    let c = unsafe { &*chain };
    if !c.prp_list_virt.is_null() {
        let prp_list_pages = (count + 511 - 1) / 511;
        unsafe { 
            iounmap(c.prp_list_virt, (prp_list_pages * PAGE_SIZE) as u32);
            pmm_free_contiguous(c.prp2 as *mut core::ffi::c_void, prp_list_pages);
        }
    }
}
