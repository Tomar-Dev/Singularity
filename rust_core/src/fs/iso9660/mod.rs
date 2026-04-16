// rust_core/src/fs/iso9660/mod.rs

use alloc::string::{String, ToString};
use alloc::vec::Vec;
use alloc::sync::Arc;
use alloc::boxed::Box;
use core::ffi::c_void;
use crate::fs::traits::{FsNode, FsBackend};
use crate::fs::utils::{read_u8, read_u32_le};

unsafe extern "C" {
    fn cpp_device_get_block_size(dev: *mut c_void) -> u32;
    fn device_read_block(dev: *mut c_void, lba: u64, count: u32, buffer: *mut u8) -> i32;
}

fn read_sector(dev: *mut c_void, lba: u64, count: u32) -> Option<Vec<u8>> {
    let bs = unsafe { cpp_device_get_block_size(dev) };
    if bs == 0 { return None; }
    let mut buf = alloc::vec![0u8; (count * bs) as usize];
    let ret = unsafe { device_read_block(dev, lba, count, buf.as_mut_ptr()) };
    if ret == 1 { 
        Some(buf) 
    } else { 
        None 
    }
}

pub struct IsoMount {
    dev: *mut c_void,
    block_size: u32,
}

unsafe impl Send for IsoMount {}
unsafe impl Sync for IsoMount {}

pub struct IsoNode {
    mount: Arc<IsoMount>,
    lba: u32,
    size: u64,
    is_dir: bool,
}

unsafe impl Send for IsoNode {}
unsafe impl Sync for IsoNode {}

fn clean_iso_name(raw: &[u8]) -> String {
    let mut s = String::new();
    for &b in raw {
        if b == b';' || b == 0 { break; }
        s.push(b as char);
    }
    if s.is_empty() { return ".".to_string(); }
    if s == "\x00" { return ".".to_string(); }
    if s == "\x01" { return "..".to_string(); }
    s
}

impl FsNode for IsoNode {
    fn get_size(&self) -> u64 { self.size }
    
    fn is_dir(&self) -> bool { self.is_dir }
    
    fn read(&self, offset: u64, buf: &mut[u8]) -> Result<usize, u32> {
        if offset >= self.size { return Ok(0); }
        let count = core::cmp::min(buf.len() as u64, self.size - offset) as usize;
        if count == 0 { return Ok(0); }
        
        let bs = self.mount.block_size as u64;
        let start_lba = self.lba as u64 + (offset / bs);
        let end_lba = self.lba as u64 + ((offset + count as u64 - 1) / bs);
        let sectors = (end_lba - start_lba + 1) as u32;
        let offset_in_sector = (offset % bs) as usize;
        
        if let Some(data) = read_sector(self.mount.dev, start_lba, sectors) {
            buf[..count].copy_from_slice(&data[offset_in_sector..offset_in_sector + count]);
            Ok(count)
        } else {
            Err(8) 
        }
    }
    
    fn write(&self, _offset: u64, _buf: &[u8]) -> Result<usize, u32> {
        Err(2) 
    }
    
    fn finddir(&self, name: &str) -> Option<FsBackend> {
        if !self.is_dir { return None; }
        let bs = self.mount.block_size as u32;
        let sectors = (self.size as u32).div_ceil(bs);
        
        for s in 0..sectors {
            if let Some(data) = read_sector(self.mount.dev, self.lba as u64 + s as u64, 1) {
                let mut offset = 0;
                while offset < bs as usize {
                    let len = read_u8(&data, offset) as usize;
                    if len == 0 { break; }
                    
                    let file_lba = read_u32_le(&data, offset + 2);
                    let file_size = read_u32_le(&data, offset + 10);
                    let flags = read_u8(&data, offset + 25);
                    let name_len = read_u8(&data, offset + 32) as usize;
                    
                    if offset + 33 + name_len <= data.len() {
                        let fname = clean_iso_name(&data[offset + 33 .. offset + 33 + name_len]);
                        if fname == name {
                            return Some(FsBackend::Iso9660(IsoNode {
                                mount: self.mount.clone(),
                                lba: file_lba,
                                size: file_size as u64,
                                is_dir: (flags & 2) != 0,
                            }));
                        }
                    } else {
                        crate::ffi::debug_print("Warning: Directory entry out of bounds.\n\0");
                    }
                    offset += len;
                }
            } else { break; }
        }
        None
    }
    
    fn readdir(&self, index: u32) -> Option<(String, bool)> {
        if !self.is_dir { return None; }
        let bs = self.mount.block_size as u32;
        let sectors = (self.size as u32).div_ceil(bs);
        let mut curr_idx = 0;
        
        for s in 0..sectors {
            if let Some(data) = read_sector(self.mount.dev, self.lba as u64 + s as u64, 1) {
                let mut offset = 0;
                while offset < bs as usize {
                    let len = read_u8(&data, offset) as usize;
                    if len == 0 { break; }
                    
                    if curr_idx == index {
                        let flags = read_u8(&data, offset + 25);
                        let name_len = read_u8(&data, offset + 32) as usize;
                        let fname = clean_iso_name(&data[offset + 33 .. offset + 33 + name_len]);
                        return Some((fname, (flags & 2) != 0));
                    } else {
                        curr_idx += 1;
                        offset += len;
                    }
                }
            } else { break; }
        }
        None
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_iso9660_mount(dev: *mut c_void) -> *mut c_void {
    let bs = unsafe { cpp_device_get_block_size(dev) };
    if bs == 0 { return core::ptr::null_mut(); }
    
    let pvd_lba = if bs == 2048 { 16 } else { 64 };
    
    if let Some(data) = read_sector(dev, pvd_lba, 1) {
        if data[0] == 1 && &data[1..6] == b"CD001" {
            let root_lba = read_u32_le(&data, 156 + 2);
            let root_size = read_u32_le(&data, 156 + 10);
            
            let mount = Arc::new(IsoMount { dev, block_size: bs });
            let node = FsBackend::Iso9660(IsoNode {
                mount,
                lba: root_lba,
                size: root_size as u64,
                is_dir: true,
            });
            
            return Box::into_raw(Box::new(node)) as *mut c_void;
        } else {
            crate::ffi::debug_print("Error: Missing CD001 Primary Volume Descriptor.\n\0");
        }
    } else {
        crate::ffi::debug_print("Error: PVD Sector read failed.\n\0");
    }
    
    core::ptr::null_mut()
}

impl Drop for IsoMount {
    fn drop(&mut self) {
        unsafe { crate::ffi::exports::device_release(self.dev); }
    }
}
