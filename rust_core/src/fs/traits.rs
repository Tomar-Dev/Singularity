// rust_core/src/fs/traits.rs

use alloc::boxed::Box;
use alloc::string::String;
use core::ffi::{c_char, c_void};
use crate::ffi::bindings::StorageKioVec;
use crate::sys::error::KernelError;

#[repr(C)]
pub struct CDirent {
    pub name: [u8; 128],
    pub inode: u32,
}

pub trait FsNode: Send + Sync {
    fn get_size(&self) -> u64;
    fn is_dir(&self) -> bool;

    fn read(&self, offset: u64, buf: &mut [u8]) -> Result<usize, u32>;
    fn write(&self, offset: u64, buf: &[u8]) -> Result<usize, u32>;

    fn read_vector(&self, offset: u64, vecs: &[StorageKioVec]) -> Result<usize, u32> {
        let _ = offset;
        let _ = vecs;
        Err(KernelError::Unsupported as u32)
    }

    fn write_vector(&self, offset: u64, vecs: &[StorageKioVec]) -> Result<usize, u32> {
        let _ = offset;
        let _ = vecs;
        Err(KernelError::Unsupported as u32)
    }

    fn finddir(&self, name: &str) -> Option<FsBackend> {
        let _ = name;
        None
    }

    fn readdir(&self, index: u32) -> Option<(String, bool)> {
        let _ = index;
        None
    }
    
    fn create_child(&self, name: &str, is_dir: bool) -> Option<FsBackend> {
        let _ = name;
        let _ = is_dir;
        None
    }
}

// STATIC DISPATCH ENUM (Zero-Cost Abstraction)
pub enum FsBackend {
    Ext4(crate::fs::ext4::ExtNode),
    Fat32(crate::fs::fat32::Fat32Node),
    Iso9660(crate::fs::iso9660::IsoNode),
    Udf(crate::fs::udf::UdfNode),
}

impl FsNode for FsBackend {
    fn get_size(&self) -> u64 {
        match self {
            FsBackend::Ext4(node) => node.get_size(),
            FsBackend::Fat32(node) => node.get_size(),
            FsBackend::Iso9660(node) => node.get_size(),
            FsBackend::Udf(node) => node.get_size(),
        }
    }

    fn is_dir(&self) -> bool {
        match self {
            FsBackend::Ext4(node) => node.is_dir(),
            FsBackend::Fat32(node) => node.is_dir(),
            FsBackend::Iso9660(node) => node.is_dir(),
            FsBackend::Udf(node) => node.is_dir(),
        }
    }

    fn read(&self, offset: u64, buf: &mut [u8]) -> Result<usize, u32> {
        match self {
            FsBackend::Ext4(node) => node.read(offset, buf),
            FsBackend::Fat32(node) => node.read(offset, buf),
            FsBackend::Iso9660(node) => node.read(offset, buf),
            FsBackend::Udf(node) => node.read(offset, buf),
        }
    }

    fn write(&self, offset: u64, buf: &[u8]) -> Result<usize, u32> {
        match self {
            FsBackend::Ext4(node) => node.write(offset, buf),
            FsBackend::Fat32(node) => node.write(offset, buf),
            FsBackend::Iso9660(node) => node.write(offset, buf),
            FsBackend::Udf(node) => node.write(offset, buf),
        }
    }

    fn read_vector(&self, offset: u64, vecs: &[StorageKioVec]) -> Result<usize, u32> {
        match self {
            FsBackend::Ext4(node) => node.read_vector(offset, vecs),
            FsBackend::Fat32(node) => node.read_vector(offset, vecs),
            FsBackend::Iso9660(node) => node.read_vector(offset, vecs),
            FsBackend::Udf(node) => node.read_vector(offset, vecs),
        }
    }

    fn write_vector(&self, offset: u64, vecs: &[StorageKioVec]) -> Result<usize, u32> {
        match self {
            FsBackend::Ext4(node) => node.write_vector(offset, vecs),
            FsBackend::Fat32(node) => node.write_vector(offset, vecs),
            FsBackend::Iso9660(node) => node.write_vector(offset, vecs),
            FsBackend::Udf(node) => node.write_vector(offset, vecs),
        }
    }

    fn finddir(&self, name: &str) -> Option<FsBackend> {
        match self {
            FsBackend::Ext4(node) => node.finddir(name),
            FsBackend::Fat32(node) => node.finddir(name),
            FsBackend::Iso9660(node) => node.finddir(name),
            FsBackend::Udf(node) => node.finddir(name),
        }
    }

    fn readdir(&self, index: u32) -> Option<(String, bool)> {
        match self {
            FsBackend::Ext4(node) => node.readdir(index),
            FsBackend::Fat32(node) => node.readdir(index),
            FsBackend::Iso9660(node) => node.readdir(index),
            FsBackend::Udf(node) => node.readdir(index),
        }
    }

    fn create_child(&self, name: &str, is_dir: bool) -> Option<FsBackend> {
        match self {
            FsBackend::Ext4(node) => node.create_child(name, is_dir),
            FsBackend::Fat32(node) => node.create_child(name, is_dir),
            FsBackend::Iso9660(node) => node.create_child(name, is_dir),
            FsBackend::Udf(node) => node.create_child(name, is_dir),
        }
    }
}

// =====================================================================
// FLAT C API (Direct Calls from C++ ProviderBlob/ProviderContainer)
// =====================================================================

#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_fs_get_size(node_ptr: *mut c_void) -> u64 {
    if node_ptr.is_null() { 
        return 0; 
    } else {
        let node = unsafe { &*(node_ptr as *mut FsBackend) };
        return node.get_size();
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_fs_is_dir(node_ptr: *mut c_void) -> i32 {
    if node_ptr.is_null() { 
        return 0; 
    } else {
        let node = unsafe { &*(node_ptr as *mut FsBackend) };
        if node.is_dir() { return 1; } else { return 0; }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_fs_read(node_ptr: *mut c_void, offset: u64, size: u32, buffer: *mut u8) -> i32 {
    if node_ptr.is_null() || buffer.is_null() { 
        return -1; 
    } else {
        let node = unsafe { &*(node_ptr as *mut FsBackend) };
        let buf_slice = unsafe { core::slice::from_raw_parts_mut(buffer, size as usize) };
        match node.read(offset, buf_slice) {
            Ok(bytes) => return bytes as i32,
            Err(_) => return -1,
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_fs_write(node_ptr: *mut c_void, offset: u64, size: u32, buffer: *const u8) -> i32 {
    if node_ptr.is_null() || buffer.is_null() { 
        return -1; 
    } else {
        let node = unsafe { &*(node_ptr as *mut FsBackend) };
        let buf_slice = unsafe { core::slice::from_raw_parts(buffer, size as usize) };
        match node.write(offset, buf_slice) {
            Ok(bytes) => return bytes as i32,
            Err(_) => return -1,
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_fs_read_vector(node_ptr: *mut c_void, offset: u64, vecs: *const StorageKioVec, count: i32) -> i32 {
    if node_ptr.is_null() || vecs.is_null() || count <= 0 { 
        return -1; 
    } else {
        let node = unsafe { &*(node_ptr as *mut FsBackend) };
        let vec_slice = unsafe { core::slice::from_raw_parts(vecs, count as usize) };
        match node.read_vector(offset, vec_slice) {
            Ok(bytes) => return bytes as i32,
            Err(_) => return -1,
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_fs_write_vector(node_ptr: *mut c_void, offset: u64, vecs: *const StorageKioVec, count: i32) -> i32 {
    if node_ptr.is_null() || vecs.is_null() || count <= 0 { 
        return -1; 
    } else {
        let node = unsafe { &*(node_ptr as *mut FsBackend) };
        let vec_slice = unsafe { core::slice::from_raw_parts(vecs, count as usize) };
        match node.write_vector(offset, vec_slice) {
            Ok(bytes) => return bytes as i32,
            Err(_) => return -1,
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_fs_finddir(node_ptr: *mut c_void, name_ptr: *const c_char) -> *mut c_void {
    if node_ptr.is_null() || name_ptr.is_null() { 
        return core::ptr::null_mut(); 
    } else {
        let node = unsafe { &*(node_ptr as *mut FsBackend) };
        let target_name = unsafe { core::ffi::CStr::from_ptr(name_ptr).to_str().unwrap_or("") };
        
        match node.finddir(target_name) {
            Some(child) => return Box::into_raw(Box::new(child)) as *mut c_void,
            None => return core::ptr::null_mut(),
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_fs_readdir(node_ptr: *mut c_void, index: u32, out_dirent: *mut CDirent) -> *mut CDirent {
    if node_ptr.is_null() || out_dirent.is_null() { 
        return core::ptr::null_mut(); 
    } else {
        let node = unsafe { &*(node_ptr as *mut FsBackend) };
        
        match node.readdir(index) {
            Some((name_str, _is_dir)) => {
                unsafe {
                    core::ptr::write_bytes((*out_dirent).name.as_mut_ptr(), 0, 128);
                    let bytes = name_str.as_bytes();
                    let copy_len = core::cmp::min(bytes.len(), 127);
                    core::ptr::copy_nonoverlapping(bytes.as_ptr(), (*out_dirent).name.as_mut_ptr(), copy_len);
                    (*out_dirent).inode = index + 1; 
                }
                return out_dirent;
            },
            None => return core::ptr::null_mut(),
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_fs_create_child(node_ptr: *mut c_void, name_ptr: *const c_char, is_dir: i32) -> *mut c_void {
    if node_ptr.is_null() || name_ptr.is_null() { 
        return core::ptr::null_mut(); 
    } else {
        let node = unsafe { &*(node_ptr as *mut FsBackend) };
        let name = unsafe { core::ffi::CStr::from_ptr(name_ptr).to_str().unwrap_or("") };
        
        match node.create_child(name, is_dir != 0) {
            Some(child) => return Box::into_raw(Box::new(child)) as *mut c_void,
            None => return core::ptr::null_mut(),
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_fs_release(node_ptr: *mut c_void) {
    if !node_ptr.is_null() {
        unsafe {
            let _ = Box::from_raw(node_ptr as *mut FsBackend);
        }
    } else {
        // Nothing to do for null pointers
    }
}