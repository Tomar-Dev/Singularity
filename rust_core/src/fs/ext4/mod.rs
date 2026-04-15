// rust_core/src/fs/ext4/mod.rs

use alloc::string::String;
use alloc::vec;
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

const EXT2_SIGNATURE: u16 = 0xEF53;
const EXT4_EXTENTS_FLAG: u32 = 0x80000;

#[derive(Clone)]
struct ExtInodeData {
    i_mode: u16,
    i_size: u32,
    i_flags: u32,
    i_block:[u32; 15],
}

pub struct ExtMount {
    dev: *mut c_void,
    block_size: u32,
    inodes_per_group: u32,
    inode_size: u32,
    bgdt_start_block: u32,
}

unsafe impl Send for ExtMount {}
unsafe impl Sync for ExtMount {}

pub struct ExtNode {
    mount: Arc<ExtMount>,
    #[allow(dead_code)]
    inode_num: u32,
    data: ExtInodeData,
    is_dir: bool,
}

unsafe impl Send for ExtNode {}
unsafe impl Sync for ExtNode {}

impl ExtMount {
    fn read_inode(&self, ino: u32) -> Option<ExtInodeData> {
        if ino == 0 { return None; }
        
        let group = (ino - 1) / self.inodes_per_group;
        let index_in_group = (ino - 1) % self.inodes_per_group;
        let bg_desc_size = 32;
        let bg_desc_lba = self.bgdt_start_block as u64 * (self.block_size as u64 / 512);
        let target_bg_sector = bg_desc_lba + ((group * bg_desc_size) / 512) as u64;
        let bg_offset = (group * bg_desc_size) % 512;
        
        let mut buf =[0u8; 512];
        let ret = unsafe { disk_cache_read_block(self.dev, target_bg_sector, 1, buf.as_mut_ptr()) };
        if ret != 1 { 
            crate::ffi::debug_print("[EXT4] Error: Failed to read Block Group Descriptor.\n\0");
            return None; 
        }
        
        let inode_table_block = read_u32_le(&buf, bg_offset as usize + 8);
        
        let inode_offset_in_table = index_in_group * self.inode_size;
        let block_in_table = inode_offset_in_table / self.block_size;
        let byte_in_block = inode_offset_in_table % self.block_size;
        
        let target_block_lba = (inode_table_block as u64 + block_in_table as u64) * (self.block_size as u64 / 512); 
        let mut inode_buf = vec![0u8; self.block_size as usize];
        
        let ret2 = unsafe { disk_cache_read_block(self.dev, target_block_lba, self.block_size / 512, inode_buf.as_mut_ptr()) };
        if ret2 != 1 { 
            crate::ffi::debug_print("[EXT4] Error: Failed to read Inode Table sector.\n\0");
            return None; 
        }
        
        let base = byte_in_block as usize;
        
        let mut i_block =[0u32; 15];
        for i in 0..15 {
            i_block[i] = read_u32_le(&inode_buf, base + 40 + (i * 4));
        }

        Some(ExtInodeData {
            i_mode: read_u16_le(&inode_buf, base),
            i_size: read_u32_le(&inode_buf, base + 4),
            i_flags: read_u32_le(&inode_buf, base + 32),
            i_block,
        })
    }

    // FIX 2: B-Tree Deep Traversal onarımı (Index düğümleri veri sanılmayacak)
    fn find_extent_block(&self, i_block: &[u32; 15], logical_block: u32) -> u32 {
        let mut header_buf = [0u8; 60];
        for i in 0..15 {
            let bytes = i_block[i].to_le_bytes();
            header_buf[i*4..i*4+4].copy_from_slice(&bytes);
        }

        let mut current_buf = header_buf.to_vec();

        loop {
            let magic = read_u16_le(&current_buf, 0);
            if magic != 0xF30A { 
                crate::ffi::debug_print("[EXT4] Error: Invalid Extent Magic.\n\0");
                return 0; 
            }

            let entries = read_u16_le(&current_buf, 2);
            let depth = read_u16_le(&current_buf, 6);

            if depth == 0 {
                // Yaprak (Leaf) Düğüm: Gerçek veriye ulaştık
                for i in 0..entries as usize {
                    let off = 12 + (i * 12);
                    let ee_block = read_u32_le(&current_buf, off);
                    let ee_len = read_u16_le(&current_buf, off + 4) & 0x7FFF; 
                    let ee_start_hi = read_u16_le(&current_buf, off + 6) as u64;
                    let ee_start_lo = read_u32_le(&current_buf, off + 8) as u64;

                    if logical_block >= ee_block && logical_block < ee_block + ee_len as u32 {
                        let phys_block = (ee_start_hi << 32) | ee_start_lo;
                        return (phys_block + (logical_block - ee_block) as u64) as u32;
                    }
                }
                return 0; // Bulunamadı
            } else {
                // İndeks (Index) Düğüm: Ağaçta daha derine inmemiz gerek
                let mut next_phys = 0;
                for i in 0..entries as usize {
                    let off = 12 + (i * 12);
                    let ei_block = read_u32_le(&current_buf, off);
                    let ei_leaf_hi = read_u16_le(&current_buf, off + 4) as u64;
                    let ei_leaf_lo = read_u32_le(&current_buf, off + 8) as u64;

                    if logical_block >= ei_block {
                        next_phys = ((ei_leaf_hi << 32) | ei_leaf_lo) as u32;
                    } else {
                        break;
                    }
                }
                
                if next_phys == 0 { return 0; }

                // İndeks bloğunu diskten oku ve döngüye devam et (Recursive Fallback)
                let bs = self.block_size;
                let target_lba = next_phys as u64 * (bs as u64 / 512);
                let mut new_buf = vec![0u8; bs as usize];
                
                let ret = unsafe { disk_cache_read_block(self.dev, target_lba, bs / 512, new_buf.as_mut_ptr()) };
                if ret != 1 {
                    crate::ffi::debug_print("[EXT4] Error: Failed to read Extent Index Block.\n\0");
                    return 0;
                }
                current_buf = new_buf;
            }
        }
    }

    fn get_physical_block(&self, data: &ExtInodeData, logical_block: u32) -> u32 {
        if (data.i_flags & EXT4_EXTENTS_FLAG) != 0 {
            self.find_extent_block(&data.i_block, logical_block)
        } else {
            if logical_block < 12 { 
                data.i_block[logical_block as usize]
            } else {
                crate::ffi::debug_print("[EXT4] Error: Indirect blocks not supported without Extents.\n\0");
                0 
            }
        }
    }
}

impl FsNode for ExtNode {
    fn get_size(&self) -> u64 { self.data.i_size as u64 }
    fn is_dir(&self) -> bool { self.is_dir }

    fn read(&self, offset: u64, buf: &mut [u8]) -> Result<usize, u32> {
        let size = self.get_size();
        if offset >= size { return Ok(0); }
        let mut count = buf.len();
        if offset + count as u64 > size {
            count = (size - offset) as usize;
        }

        let mut remaining = count;
        let mut file_offset = offset;
        let mut out_idx = 0;

        let bs = self.mount.block_size;

        while remaining > 0 {
            let logical_block = (file_offset / bs as u64) as u32;
            let offset_in_block = (file_offset % bs as u64) as u32;
            
            let phys_block = self.mount.get_physical_block(&self.data, logical_block);
            
            let max_blocks = ((remaining as u32 + offset_in_block + bs - 1) / bs) as u32;
            let mut contiguous_blocks = 1;

            while contiguous_blocks < max_blocks && phys_block != 0 {
                let next_phys = self.mount.get_physical_block(&self.data, logical_block + contiguous_blocks);
                if next_phys == phys_block + contiguous_blocks {
                    contiguous_blocks += 1;
                } else {
                    break;
                }
            }

            let mut bytes_to_transfer = (contiguous_blocks * bs) - offset_in_block;
            if bytes_to_transfer > remaining as u32 {
                bytes_to_transfer = remaining as u32;
            }

            if phys_block == 0 {
                for i in 0..bytes_to_transfer as usize {
                    buf[out_idx + i] = 0;
                }
            } else {
                let start_lba = (phys_block as u64 * (bs as u64 / 512)) + (offset_in_block as u64 / 512);

                let dest_ptr = unsafe { buf.as_mut_ptr().add(out_idx) };
                let is_aligned = (dest_ptr as u64 & 0x1FF) == 0 && (bytes_to_transfer % 512) == 0;

                if is_aligned && bytes_to_transfer >= 4096 {
                    let vec = StorageKioVec {
                        phys_addr: 0,
                        virt_addr: dest_ptr as *mut c_void,
                        size: bytes_to_transfer as usize,
                    };
                    let ret = unsafe { disk_cache_read_vector(self.mount.dev, start_lba, &vec, 1) };
                    if ret != 1 { 
                        crate::ffi::debug_print("[EXT4] DMA Vector Read Failed.\n\0");
                        return Err(8); 
                    }
                } else {
                    let sectors_to_read = (bytes_to_transfer + 511) / 512;
                    let bounce_size = sectors_to_read as usize * 512;
                    
                    let bounce_buf = unsafe { kmalloc_contiguous(bounce_size) as *mut u8 };
                    if bounce_buf.is_null() { return Err(4); }

                    let ret = unsafe { disk_cache_read_block(self.mount.dev, start_lba, sectors_to_read, bounce_buf) };
                    if ret != 1 {
                        unsafe { kfree_contiguous(bounce_buf as *mut c_void, bounce_size); }
                        crate::ffi::debug_print("[EXT4] Block Read Failed.\n\0");
                        return Err(8);
                    }

                    unsafe {
                        core::ptr::copy_nonoverlapping(bounce_buf, buf.as_mut_ptr().add(out_idx), bytes_to_transfer as usize);
                        kfree_contiguous(bounce_buf as *mut c_void, bounce_size);
                    }
                }
            }

            out_idx += bytes_to_transfer as usize;
            file_offset += bytes_to_transfer as u64;
            remaining -= bytes_to_transfer as usize;
        }

        Ok(count)
    }

    fn write(&self, _offset: u64, _buf: &[u8]) -> Result<usize, u32> {
        Err(2)
    }

    fn finddir(&self, name: &str) -> Option<FsBackend> {
        if !self.is_dir { return None; }
        
        let bs = self.mount.block_size;
        let max_blocks = (self.data.i_size + bs - 1) / bs;
        let mut dir_buf = vec![0u8; bs as usize];

        for block_idx in 0..max_blocks {
            let phys = self.mount.get_physical_block(&self.data, block_idx);
            if phys == 0 { break; }

            let ret = unsafe { disk_cache_read_block(self.mount.dev, phys as u64 * (bs as u64 / 512), bs / 512, dir_buf.as_mut_ptr()) };
            if ret != 1 { 
                crate::ffi::debug_print("[EXT4] Warning: Directory block read failed.\n\0");
                continue; 
            }

            let mut offset = 0;
            while offset < bs as usize {
                let rec_len = read_u16_le(&dir_buf, offset + 4) as usize;
                if rec_len == 0 || rec_len > bs as usize - offset { break; }

                let inode = read_u32_le(&dir_buf, offset);
                let name_len = read_u8(&dir_buf, offset + 6) as usize;
                if offset + 8 + name_len > bs as usize { break; } 

                if inode != 0 {
                    let mut entry_name = String::new();
                    for i in 0..name_len {
                        entry_name.push(dir_buf[offset + 8 + i] as char);
                    }

                    if entry_name == name {
                        if let Some(target_data) = self.mount.read_inode(inode) {
                            let is_directory = (target_data.i_mode & 0xF000) == 0x4000;
                            return Some(FsBackend::Ext4(ExtNode {
                                mount: self.mount.clone(),
                                inode_num: inode,
                                data: target_data,
                                is_dir: is_directory,
                            }));
                        } else {
                            crate::ffi::debug_print("[EXT4] Error: Could not resolve target inode.\n\0");
                        }
                    }
                }
                offset += rec_len;
            }
        }
        None
    }

    fn readdir(&self, index: u32) -> Option<(String, bool)> {
        if !self.is_dir { return None; }
        
        let bs = self.mount.block_size;
        let max_blocks = (self.data.i_size + bs - 1) / bs;
        let mut dir_buf = vec![0u8; bs as usize];
        let mut curr_idx = 0;

        for block_idx in 0..max_blocks {
            let phys = self.mount.get_physical_block(&self.data, block_idx);
            if phys == 0 { break; }

            let ret = unsafe { disk_cache_read_block(self.mount.dev, phys as u64 * (bs as u64 / 512), bs / 512, dir_buf.as_mut_ptr()) };
            if ret != 1 { continue; }

            let mut offset = 0;
            while offset < bs as usize {
                let rec_len = read_u16_le(&dir_buf, offset + 4) as usize;
                if rec_len == 0 || rec_len > bs as usize - offset { break; }

                let inode = read_u32_le(&dir_buf, offset);
                let name_len = read_u8(&dir_buf, offset + 6) as usize;
                let file_type = read_u8(&dir_buf, offset + 7); 
                if offset + 8 + name_len > bs as usize { break; } 

                if inode != 0 {
                    if curr_idx == index {
                        let mut entry_name = String::new();
                        for i in 0..name_len {
                            entry_name.push(dir_buf[offset + 8 + i] as char);
                        }
                        return Some((entry_name, file_type == 2));
                    } else {
                        curr_idx += 1;
                    }
                }
                offset += rec_len;
            }
        }
        None
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_ext4_mount(dev: *mut c_void) -> *mut c_void {
    let mut buf = [0u8; 1024];
    let ret = unsafe { disk_cache_read_block(dev, 2, 2, buf.as_mut_ptr()) };
    if ret != 1 { 
        crate::ffi::debug_print("[EXT4] Error: Superblock read failed.\n\0");
        return core::ptr::null_mut(); 
    }

    let magic = read_u16_le(&buf, 56);
    if magic == EXT2_SIGNATURE { 
        let log_block_size = read_u32_le(&buf, 24);
        let block_size = 1024 << log_block_size;
        let inodes_per_group = read_u32_le(&buf, 40);
        let rev_level = read_u32_le(&buf, 76);
        let inode_size = if rev_level >= 1 { read_u16_le(&buf, 88) as u32 } else { 128 };
        let bgdt_start_block = if block_size == 1024 { 2 } else { 1 };

        let mount = Arc::new(ExtMount {
            dev,
            block_size,
            inodes_per_group,
            inode_size,
            bgdt_start_block,
        });

        if let Some(root_data) = mount.read_inode(2) {
            let node = FsBackend::Ext4(ExtNode {
                mount,
                inode_num: 2,
                data: root_data,
                is_dir: true,
            });
            return Box::into_raw(Box::new(node)) as *mut c_void;
        } else {
            crate::ffi::debug_print("[EXT4] Error: Root inode (2) could not be read.\n\0");
        }
    } else {
        crate::ffi::debug_print("[EXT4] Error: EXT2/4 Magic Signature (0xEF53) not found.\n\0");
    }
    core::ptr::null_mut()
}

impl Drop for ExtMount {
    fn drop(&mut self) {
        unsafe { crate::ffi::exports::device_release(self.dev); }
    }
}