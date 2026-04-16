// rust_core/src/fs/fat32/mod.rs

use alloc::string::String;
use alloc::sync::Arc;
use alloc::boxed::Box;
use core::ffi::c_void;
use crate::fs::traits::{FsNode, FsBackend};
use crate::fs::utils::{read_u8, read_u16_le, read_u32_le};
use crate::ffi::bindings::StorageKioVec;

unsafe extern "C" {
    fn disk_cache_read_block(dev: *mut c_void, lba: u64, count: u32, buffer: *mut u8) -> i32;
    fn disk_cache_read_vector(dev: *mut c_void, lba: u64, vectors: *const StorageKioVec, count: i32) -> i32;
    fn kmalloc_contiguous(size: usize) -> *mut c_void;
    fn kfree_contiguous(ptr: *mut c_void, size: usize);
}

pub struct Fat32Mount {
    dev: *mut c_void,
    first_data_sector: u32,
    sectors_per_cluster: u32,
    bytes_per_cluster: u32,
    fat_start_sector: u32,
}

unsafe impl Send for Fat32Mount {}
unsafe impl Sync for Fat32Mount {}

impl Drop for Fat32Mount {
    fn drop(&mut self) {
        unsafe { crate::ffi::exports::device_release(self.dev); }
    }
}

pub struct Fat32Node {
    mount: Arc<Fat32Mount>,
    start_cluster: u32,
    size: u64,
    is_dir: bool,
}

unsafe impl Send for Fat32Node {}
unsafe impl Sync for Fat32Node {}

impl Fat32Mount {
    fn cluster_to_lba(&self, cluster: u32) -> u64 {
        // FIX: Cluster Underflow Koruması
        if cluster < 2 { return 0; }
        self.first_data_sector as u64 + ((cluster - 2) as u64 * self.sectors_per_cluster as u64)
    }

    fn get_next_cluster(&self, cluster: u32) -> u32 {
        if cluster < 2 { return 0x0FFFFFFF; } // FIX: Geçersiz küme araması engellendi
        let sector = self.fat_start_sector + (cluster * 4 / 512);
        let offset = (cluster * 4) % 512;
        let mut buf = alloc::vec![0u8; 4096];
        
        let ret = unsafe { disk_cache_read_block(self.dev, sector as u64, 1, buf.as_mut_ptr()) };
        if ret != 1 {
            crate::ffi::debug_print("Error: Failed to read FAT sector.\n\0");
            return 0x0FFFFFFF;
        } else {
            let next = read_u32_le(&buf, offset as usize);
            return next & 0x0FFFFFFF;
        }
    }
}

fn lfn_to_ascii(buf: &[u8], offset: usize, count: usize, dest: &mut String) {
    for i in 0..count {
        let char_val = read_u16_le(buf, offset + (i * 2));
        if char_val == 0 || char_val == 0xFFFF { break; }
        
        if dest.len() >= 255 { break; }
        if char_val < 128 {
            dest.push(char_val as u8 as char);
        } else {
            dest.push('_');
        }
    }
}

fn get_short_name(buf: &[u8]) -> String {
    let mut name = String::new();
    for i in 0..8 {
        if buf[i] == b' ' { break; }
        name.push(buf[i] as char);
    }
    if buf[8] != b' ' {
        name.push('.');
        for i in 8..11 {
            if buf[i] == b' ' { break; }
            name.push(buf[i] as char);
        }
    }
    name
}

impl FsNode for Fat32Node {
    fn get_size(&self) -> u64 { self.size }
    fn is_dir(&self) -> bool { self.is_dir }

    fn read(&self, offset: u64, buf: &mut[u8]) -> Result<usize, u32> {
        if offset >= self.size && !self.is_dir { return Ok(0); }
        
        let mut count = buf.len();
        if !self.is_dir && offset + count as u64 > self.size {
            count = (self.size - offset) as usize;
        }

        let mut cluster = self.start_cluster;
        let mut cluster_chain_idx = (offset / self.mount.bytes_per_cluster as u64) as u32;
        let mut offset_in_cluster = (offset % self.mount.bytes_per_cluster as u64) as u32;

        while cluster_chain_idx > 0 {
            cluster = self.mount.get_next_cluster(cluster);
            if cluster < 2 || cluster >= 0x0FFFFFF8 { return Err(8); }
            cluster_chain_idx -= 1;
        }

        let mut remaining = count;
        let mut out_idx = 0;

        while remaining > 0 && cluster >= 2 && cluster < 0x0FFFFFF8 {
            let mut contiguous_clusters = 1;
            let mut current_cluster = cluster;

            loop {
                if (contiguous_clusters * self.mount.bytes_per_cluster) - offset_in_cluster >= remaining as u32 {
                    break;
                }
                let next_cluster = self.mount.get_next_cluster(current_cluster);
                if next_cluster == current_cluster + 1 && next_cluster >= 2 && next_cluster < 0x0FFFFFF8 {
                    contiguous_clusters += 1;
                    current_cluster = next_cluster;
                } else {
                    break;
                }
            }

            let mut bytes_to_transfer = (contiguous_clusters * self.mount.bytes_per_cluster) - offset_in_cluster;
            if bytes_to_transfer > remaining as u32 {
                bytes_to_transfer = remaining as u32;
            }

            let start_lba = self.mount.cluster_to_lba(cluster) + (offset_in_cluster as u64 / 512);
            
            let dest_ptr = unsafe { buf.as_mut_ptr().add(out_idx) };
            let is_aligned = (dest_ptr as u64 & 0x1FF) == 0 && (bytes_to_transfer % 512) == 0;

            if is_aligned && bytes_to_transfer >= 4096 {
                let vec = StorageKioVec {
                    phys_addr: 0,
                    virt_addr: dest_ptr as *mut c_void,
                    size: bytes_to_transfer as usize,
                };
                let ret = unsafe { disk_cache_read_vector(self.mount.dev, start_lba, &vec, 1) };
                if ret != 1 { return Err(8); }
            } else {
                let sectors_to_read = (bytes_to_transfer + 511) / 512;
                let bounce_size = sectors_to_read as usize * 512;
                
                let bounce_buf = unsafe { kmalloc_contiguous(bounce_size) as *mut u8 };
                if bounce_buf.is_null() { return Err(4); }

                let ret = unsafe { disk_cache_read_block(self.mount.dev, start_lba, sectors_to_read, bounce_buf) };
                if ret != 1 {
                    unsafe { kfree_contiguous(bounce_buf as *mut c_void, bounce_size); }
                    return Err(8);
                }

                unsafe {
                    core::ptr::copy_nonoverlapping(bounce_buf, buf.as_mut_ptr().add(out_idx), bytes_to_transfer as usize);
                    kfree_contiguous(bounce_buf as *mut c_void, bounce_size);
                }
            }

            out_idx += bytes_to_transfer as usize;
            remaining -= bytes_to_transfer as usize;
            offset_in_cluster = 0;

            cluster = self.mount.get_next_cluster(current_cluster);
        }

        Ok(count - remaining)
    }

    fn write(&self, _offset: u64, _buf: &[u8]) -> Result<usize, u32> {
        Err(2)
    }

    fn finddir(&self, name: &str) -> Option<FsBackend> {
        if !self.is_dir { return None; }
        
        let mut cluster = self.start_cluster;
        let mut cl_buf = alloc::vec![0u8; self.mount.bytes_per_cluster as usize];
        let mut current_lfn = String::new();

        while cluster >= 2 && cluster < 0x0FFFFFF8 {
            let lba = self.mount.cluster_to_lba(cluster);
            let ret = unsafe { disk_cache_read_block(self.mount.dev, lba, self.mount.sectors_per_cluster, cl_buf.as_mut_ptr()) };
            if ret != 1 { 
                crate::ffi::debug_print("Error: Directory cluster read failed.\n\0");
                return None; 
            }

            let mut offset = 0;
            while offset < self.mount.bytes_per_cluster as usize {
                if cl_buf[offset] == 0x00 { return None; }
                if cl_buf[offset] == 0xE5 {
                    current_lfn.clear();
                    offset += 32;
                    continue;
                }

                let attr = read_u8(&cl_buf, offset + 11);
                if attr == 0x0F {
                    let order = read_u8(&cl_buf, offset);
                    if (order & 0x40) != 0 { current_lfn.clear(); }
                    if (order & 0x3F) > 0 && (order & 0x3F) <= 20 {
                        let mut part = String::new();
                        lfn_to_ascii(&cl_buf, offset + 1, 5, &mut part);
                        lfn_to_ascii(&cl_buf, offset + 14, 6, &mut part);
                        lfn_to_ascii(&cl_buf, offset + 28, 2, &mut part);
                        current_lfn.insert_str(0, &part); 
                    } else {
                        crate::ffi::debug_print("Warning: Corrupted LFN entry detected.\n\0");
                    }
                } else if (attr & 0x08) == 0 {
                    let mut final_name = current_lfn.clone();
                    if final_name.is_empty() {
                        final_name = get_short_name(&cl_buf[offset..offset+11]);
                    }

                    if final_name == name {
                        let cluster_hi = read_u16_le(&cl_buf, offset + 20) as u32;
                        let cluster_lo = read_u16_le(&cl_buf, offset + 26) as u32;
                        let target_cluster = (cluster_hi << 16) | cluster_lo;
                        let size = read_u32_le(&cl_buf, offset + 28) as u64;

                        return Some(FsBackend::Fat32(Fat32Node {
                            mount: self.mount.clone(),
                            start_cluster: target_cluster,
                            size,
                            is_dir: (attr & 0x10) != 0,
                        }));
                    }
                    current_lfn.clear();
                } else {
                }
                offset += 32;
            }
            cluster = self.mount.get_next_cluster(cluster);
        }
        None
    }

    fn readdir(&self, index: u32) -> Option<(String, bool)> {
        if !self.is_dir { return None; }
        
        let mut cluster = self.start_cluster;
        let mut cl_buf = alloc::vec![0u8; self.mount.bytes_per_cluster as usize];
        let mut current_lfn = String::new();
        let mut valid_idx = 0;

        while cluster >= 2 && cluster < 0x0FFFFFF8 {
            let lba = self.mount.cluster_to_lba(cluster);
            let ret = unsafe { disk_cache_read_block(self.mount.dev, lba, self.mount.sectors_per_cluster, cl_buf.as_mut_ptr()) };
            if ret != 1 { return None; }

            let mut offset = 0;
            while offset < self.mount.bytes_per_cluster as usize {
                if cl_buf[offset] == 0x00 { return None; }
                if cl_buf[offset] == 0xE5 {
                    current_lfn.clear();
                    offset += 32;
                    continue;
                }

                let attr = read_u8(&cl_buf, offset + 11);
                if attr == 0x0F {
                    let order = read_u8(&cl_buf, offset);
                    if (order & 0x40) != 0 { current_lfn.clear(); }
                    if (order & 0x3F) > 0 && (order & 0x3F) <= 20 {
                        let mut part = String::new();
                        lfn_to_ascii(&cl_buf, offset + 1, 5, &mut part);
                        lfn_to_ascii(&cl_buf, offset + 14, 6, &mut part);
                        lfn_to_ascii(&cl_buf, offset + 28, 2, &mut part);
                        current_lfn.insert_str(0, &part); 
                    }
                } else if (attr & 0x08) == 0 {
                    if valid_idx == index {
                        let mut final_name = current_lfn.clone();
                        if final_name.is_empty() {
                            final_name = get_short_name(&cl_buf[offset..offset+11]);
                        }
                        return Some((final_name, (attr & 0x10) != 0));
                    } else {
                        valid_idx += 1;
                        current_lfn.clear();
                    }
                }
                offset += 32;
            }
            cluster = self.mount.get_next_cluster(cluster);
        }
        None
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_fat32_mount(dev: *mut c_void) -> *mut c_void {
    let mut buf =[0u8; 512];
    let ret = unsafe { disk_cache_read_block(dev, 0, 1, buf.as_mut_ptr()) };
    if ret != 1 { 
        crate::ffi::debug_print("Error: Boot sector read failed.\n\0");
        return core::ptr::null_mut(); 
    }

    if buf[510] == 0x55 && buf[511] == 0xAA {
        let bytes_per_sector = read_u16_le(&buf, 11) as u32;
        let sectors_per_cluster = read_u8(&buf, 13) as u32;
        let reserved_sectors = read_u16_le(&buf, 14) as u32;
        let fats_count = read_u8(&buf, 16) as u32;
        let sectors_per_fat_32 = read_u32_le(&buf, 36);
        let root_cluster = read_u32_le(&buf, 44);

        if bytes_per_sector == 0 || sectors_per_cluster == 0 {
            crate::ffi::debug_print("Error: Invalid cluster geometry.\n\0");
            return core::ptr::null_mut();
        }

        let fat_start_sector = reserved_sectors;
        let first_data_sector = reserved_sectors + (fats_count * sectors_per_fat_32);

        let mount = Arc::new(Fat32Mount {
            dev,
            first_data_sector,
            sectors_per_cluster,
            bytes_per_cluster: bytes_per_sector * sectors_per_cluster,
            fat_start_sector,
        });

        let node = FsBackend::Fat32(Fat32Node {
            mount,
            start_cluster: root_cluster,
            size: 0,
            is_dir: true,
        });

        return Box::into_raw(Box::new(node)) as *mut c_void;
    } else {
        crate::ffi::debug_print("Error: Missing 55 AA Boot Signature.\n\0");
    }
    core::ptr::null_mut()
}