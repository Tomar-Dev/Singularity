// rust_core/src/fs/udf/mod.rs

use alloc::string::String;
use alloc::vec::Vec;
use alloc::vec;
use alloc::sync::Arc;
use alloc::boxed::Box;
use core::ffi::c_void;
use crate::fs::traits::{FsNode, FsBackend};
use crate::fs::utils::{read_u8, read_u16_le, read_u32_le, read_u64_le};

unsafe extern "C" {
    fn device_read_block(dev: *mut c_void, lba: u64, count: u32, buffer: *mut u8) -> i32;
    fn cpp_device_get_block_size(dev: *mut c_void) -> u32; 
}

const TAGID_AVDP: u16 = 2;
const TAGID_PD: u16 = 5;
const TAGID_LVD: u16 = 6;
const TAGID_TD: u16 = 8;
const TAGID_FSD: u16 = 256;
const TAGID_FID: u16 = 257;
const TAGID_ICB: u16 = 261;
const TAGID_EFE: u16 = 266;

fn decode_osta_string(buf: &[u8]) -> String {
    if buf.is_empty() { return String::new(); }
    let comp_id = buf[0];
    let len = buf.len();
    let mut res = String::new();
    
    if comp_id == 8 { 
        for &byte in buf.iter().take(len).skip(1) {
            res.push(byte as char);
        }
    } else if comp_id == 16 { 
        let mut i = 1;
        while i < len - 1 {
            let c = (buf[i] as u16) << 8 | (buf[i+1] as u16);
            if let Some(chr) = core::char::from_u32(c as u32) {
                res.push(chr);
            } else {
                res.push('?');
            }
            i += 2;
        }
    } else {
        res.push_str("UNKNOWN_OSTA");
    }
    res
}

fn verify_tag(buf: &[u8], expected_id: u16) -> bool {
    if buf.len() < 16 { return false; }
    let id = read_u16_le(buf, 0);
    
    if id != expected_id { return false; }
    
    let checksum = buf[4];
    let mut sum: u8 = 0;
    
    for (i, &byte) in buf.iter().enumerate().take(16) {
        if i != 4 { sum = sum.wrapping_add(byte); }
    }
    
    if sum != checksum { return false; }
    true
}

#[derive(Clone, Copy, Debug)]
struct LongAd {
    len: u32,
    loc: u32,
    part_ref: u16,
}

pub struct UdfMount {
    device: *mut c_void,
    partition_start: u32,
    block_size: u32,
}

unsafe impl Send for UdfMount {}
unsafe impl Sync for UdfMount {}

pub struct UdfNode {
    mount: Arc<UdfMount>,
    icb_loc: LongAd,
    size: u64,
    is_dir: bool,
}

unsafe impl Send for UdfNode {}
unsafe impl Sync for UdfNode {}

fn read_sector(dev: *mut c_void, lba: u64, count: u32) -> Option<Vec<u8>> {
    let bs = unsafe { cpp_device_get_block_size(dev) };
    let size = (count * bs) as usize;
    let mut buf = vec![0u8; size];
    unsafe {
        let ret = device_read_block(dev, lba, count, buf.as_mut_ptr());
        if ret == 1 { Some(buf) } else { None }
    }
}

fn find_anchor(dev: *mut c_void) -> Option<(u32, u32, u32)> { 
    if let Some(buf) = read_sector(dev, 256, 4) {
        if verify_tag(&buf, TAGID_AVDP) {
            let loc = read_u32_le(&buf, 16 + 4); 
            let len = read_u32_le(&buf, 16);     
            return Some((loc, len, 512));
        } else {
            crate::ffi::debug_print("[UDF] Anchor (AVDP) not found at Sector 256.\n\0");
        }
    }
    
    if let Some(buf) = read_sector(dev, 1024, 4) {
        if verify_tag(&buf, TAGID_AVDP) {
            let loc = read_u32_le(&buf, 16 + 4);
            let len = read_u32_le(&buf, 16);
            return Some((loc, len, 2048));
        } else {
            crate::ffi::debug_print("[UDF] Anchor (AVDP) not found at Sector 1024.\n\0");
        }
    }
    
    None
}

fn read_node_data(node: &UdfNode, offset: u64, size: u32) -> Option<Vec<u8>> {
    if size == 0 { return None; }
    let mult = node.mount.block_size / 512;
    let sector = (node.mount.partition_start + node.icb_loc.loc) * mult;
    
    let buf = read_sector(node.mount.device, sector as u64, 4)?;
    let tag_id = read_u16_le(&buf, 0);
    
    let (info_len, ad_len_off, base_off) = if tag_id == TAGID_ICB {
        (read_u64_le(&buf, 56), 172, 176)
    } else if tag_id == TAGID_EFE {
        (read_u64_le(&buf, 64), 212, 216)
    } else {
        return None;
    };
    
    let flags = read_u16_le(&buf, 16 + 2);
    let mut ad_type = (flags & 0x07) as u8;
    let len_ad = read_u32_le(&buf, ad_len_off);
    let len_ea = read_u32_le(&buf, ad_len_off - 4);
    let ad_offset = base_off + len_ea as usize;
    
    if ad_type == 0 && info_len > 0 && info_len == len_ad as u64 && info_len < 2048 {
        ad_type = 3;
    }
    
    if ad_type == 3 {
        if offset >= info_len || info_len < offset { return Some(Vec::new()); }
        let avail = (info_len - offset) as usize;
        let to_read = core::cmp::min(avail, size as usize);
        let start = ad_offset + offset as usize;
        let mut res = vec![0u8; to_read];
        res.copy_from_slice(&buf[start..start+to_read]);
        return Some(res);
    }
    
    if ad_type == 0 || ad_type == 1 {
        let mut curr_off = 0;
        let mut ad_ptr = ad_offset;
        let ad_end = ad_offset + len_ad as usize;
        let ad_step = if ad_type == 0 { 8 } else { 16 };
        
        while ad_ptr < ad_end {
            let len = read_u32_le(&buf, ad_ptr) & 0x3FFFFFFF;
            let loc = read_u32_le(&buf, ad_ptr + 4);
            ad_ptr += ad_step;
            
            if len == 0 { break; }
            
            if offset < (curr_off + len as u64) {
                let rel_off = offset - curr_off;
                let avail = (len as u64 - rel_off) as usize;
                let to_read = core::cmp::min(avail, size as usize);
                
                let start_sector = (node.mount.partition_start + loc) * mult + (rel_off as u32 / 512);
                let offset_in_sector = (rel_off % 512) as usize;
                let sectors = (offset_in_sector + to_read).div_ceil(512); 
                
                if let Some(data) = read_sector(node.mount.device, start_sector as u64, sectors as u32) {
                    let mut res = vec![0u8; to_read];
                    res.copy_from_slice(&data[offset_in_sector..offset_in_sector+to_read]);
                    return Some(res);
                } else {
                    crate::ffi::debug_print("[UDF] Error: Failed to read allocated data sector.\n\0");
                    return None;
                }
            } else {
                curr_off += len as u64;
            }
        }
    } else {
        crate::ffi::debug_print("[UDF] Error: Unsupported allocation descriptor type.\n\0");
    }
    
    None
}

fn initialize_node(mount: Arc<UdfMount>, icb_loc: LongAd) -> Option<UdfNode> {
    let mult = mount.block_size / 512;
    let sector = (mount.partition_start + icb_loc.loc) * mult;
    
    if let Some(buf) = read_sector(mount.device, sector as u64, 4) {
        let tag_id = read_u16_le(&buf, 0);
        let (icb_tag_off, info_len) = if tag_id == TAGID_ICB {
            (16, read_u64_le(&buf, 56))
        } else if tag_id == TAGID_EFE {
            (16, read_u64_le(&buf, 64))
        } else {
            crate::ffi::debug_print("[UDF] Error: Invalid ICB Tag ID.\n\0");
            return None;
        };
        
        let file_type = read_u8(&buf, icb_tag_off + 11);
        Some(UdfNode {
            mount,
            icb_loc,
            size: info_len,
            is_dir: file_type == 4,
        })
    } else { None }
}

impl FsNode for UdfNode {
    fn get_size(&self) -> u64 { self.size }
    fn is_dir(&self) -> bool { self.is_dir }
    
    fn read(&self, offset: u64, buf: &mut [u8]) -> Result<usize, u32> {
        if let Some(data) = read_node_data(self, offset, buf.len() as u32) {
            buf[..data.len()].copy_from_slice(&data);
            Ok(data.len())
        } else {
            Err(8)
        }
    }
    
    fn write(&self, _offset: u64, _buf: &[u8]) -> Result<usize, u32> {
        Err(2)
    }
    
    fn finddir(&self, name: &str) -> Option<FsBackend> {
        if !self.is_dir { return None; }
        let dir_size = core::cmp::min(self.size, 65536) as u32;
        
        if let Some(dir_data) = read_node_data(self, 0, dir_size) {
            let mut offset = 0;
            while offset < dir_data.len() {
                if offset + 38 > dir_data.len() { break; }
                let tag_id = read_u16_le(&dir_data, offset);
                if tag_id == 0 { break; }
                
                if tag_id != TAGID_FID {
                    offset += 4;
                    offset = (offset + 3) & !3;
                    continue;
                }
                
                let file_char = read_u8(&dir_data, offset + 18);
                let file_id_len = read_u8(&dir_data, offset + 19) as usize;
                let imp_use_len = read_u16_le(&dir_data, offset + 36) as usize;
                
                if (file_char & 4) == 0 {
                    let name_offset = offset + 38 + imp_use_len;
                    if name_offset + file_id_len <= dir_data.len() {
                        let name_str = decode_osta_string(&dir_data[name_offset..name_offset+file_id_len]);
                        if name_str == name {
                            let icb_len = read_u32_le(&dir_data, offset + 20);
                            let icb_loc = read_u32_le(&dir_data, offset + 20 + 4);
                            let icb_ref = read_u16_le(&dir_data, offset + 20 + 8);
                            
                            if let Some(child_node) = initialize_node(self.mount.clone(), LongAd { len: icb_len, loc: icb_loc, part_ref: icb_ref }) {
                                return Some(FsBackend::Udf(child_node));
                            } else {
                                crate::ffi::debug_print("[UDF] Error: Failed to initialize child node from FID.\n\0");
                            }
                        }
                    } else {
                        crate::ffi::debug_print("[UDF] Warning: FID name buffer out of bounds.\n\0");
                    }
                }
                
                let entry_size = 38 + imp_use_len + file_id_len;
                let next_offset = offset + ((entry_size + 3) & !3);
                if next_offset <= offset { break; }
                offset = next_offset;
            }
        }
        None
    }
    
    fn readdir(&self, index: u32) -> Option<(String, bool)> {
        if !self.is_dir { return None; }
        let dir_size = core::cmp::min(self.size, 65536) as u32;
        
        if let Some(dir_data) = read_node_data(self, 0, dir_size) {
            let mut offset = 0;
            let mut current_idx = 0;
            
            while offset < dir_data.len() {
                if offset + 38 > dir_data.len() { break; }
                let tag_id = read_u16_le(&dir_data, offset);
                if tag_id == 0 { break; }
                
                if tag_id != TAGID_FID {
                    offset += 4;
                    offset = (offset + 3) & !3;
                    continue;
                }
                
                let file_char = read_u8(&dir_data, offset + 18);
                let file_id_len = read_u8(&dir_data, offset + 19) as usize;
                let imp_use_len = read_u16_le(&dir_data, offset + 36) as usize;
                
                if (file_char & 4) == 0 {
                    let name_offset = offset + 38 + imp_use_len;
                    if name_offset + file_id_len <= dir_data.len() {
                         let name_str = decode_osta_string(&dir_data[name_offset..name_offset+file_id_len]);
                         if !name_str.is_empty() {
                             if current_idx == index {
                                 return Some((name_str, false));
                             } else {
                                 current_idx += 1;
                             }
                         }
                    }
                }
                
                let entry_size = 38 + imp_use_len + file_id_len;
                let next_offset = offset + ((entry_size + 3) & !3);
                if next_offset <= offset { break; }
                offset = next_offset;
            }
        }
        None
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_udf_mount(dev: *mut c_void) -> *mut c_void {
    if let Some((_, mvds_len, bs)) = find_anchor(dev) {
        let mult = bs / 512;
        let sectors = mvds_len.div_ceil(bs); 
        let lba = (256 * mult) as u64;
        
        if let Some(mvds) = read_sector(dev, lba, sectors) {
            let mut offset = 0;
            let mut part_start = 0;
            let mut lvd_found = false;
            let mut fsd_loc = LongAd { len: 0, loc: 0, part_ref: 0 };
            
            while offset + 16 <= mvds.len() {
                let tag_id = read_u16_le(&mvds, offset);
                if tag_id == TAGID_TD { break; }
                
                if tag_id == TAGID_PD && verify_tag(&mvds[offset..], TAGID_PD) {
                    let p_num = read_u16_le(&mvds, offset + 22);
                    if p_num == 0 {
                        part_start = read_u32_le(&mvds, offset + 188); 
                    }
                }
                
                if tag_id == TAGID_LVD && verify_tag(&mvds[offset..], TAGID_LVD) {
                    let base = offset + 248;
                    fsd_loc.len = read_u32_le(&mvds, base);
                    fsd_loc.loc = read_u32_le(&mvds, base + 4);
                    fsd_loc.part_ref = read_u16_le(&mvds, base + 8);
                    lvd_found = true;
                }
                
                offset += bs as usize;
            }
            
            if lvd_found {
                let fsd_sector = (part_start + fsd_loc.loc) * mult;
                if let Some(fsd_buf) = read_sector(dev, fsd_sector as u64, 4) {
                    if verify_tag(&fsd_buf, TAGID_FSD) {
                        let root_base = 400;
                        let root_icb = LongAd {
                            len: read_u32_le(&fsd_buf, root_base),
                            loc: read_u32_le(&fsd_buf, root_base + 4),
                            part_ref: read_u16_le(&fsd_buf, root_base + 8),
                        };
                        
                        let mount = Arc::new(UdfMount {
                            device: dev,
                            partition_start: part_start,
                            block_size: bs,
                        });
                        
                        if let Some(root_node) = initialize_node(mount, root_icb) {
                            let node = FsBackend::Udf(root_node);
                            return Box::into_raw(Box::new(node)) as *mut c_void;
                        } else {
                            crate::ffi::debug_print("[UDF] Error: Failed to init root node.\n\0");
                        }
                    } else {
                        crate::ffi::debug_print("[UDF] Error: FSD Tag verification failed.\n\0");
                    }
                } else {
                    crate::ffi::debug_print("[UDF] Error: Failed to read FSD sector.\n\0");
                }
            } else {
                crate::ffi::debug_print("[UDF] Error: LVD (Logical Volume Descriptor) not found.\n\0");
            }
        } else {
            crate::ffi::debug_print("[UDF] Error: Failed to read MVDS.\n\0");
        }
    } else {
        crate::ffi::debug_print("[UDF] Error: Anchor volume descriptor missing.\n\0");
    }
    core::ptr::null_mut()
}

impl Drop for UdfMount {
    fn drop(&mut self) {
        unsafe { crate::ffi::exports::device_release(self.device); }
    }
}