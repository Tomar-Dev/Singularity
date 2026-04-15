// rust_core/src/storage/partition/gpt.rs

use core::ffi::{c_void, c_char, CStr};
use core::ptr;
use alloc::vec::Vec;
use alloc::vec;
use alloc::format;

use crate::kom::device::{ons_enumerate, device_get, cpp_device_get_type, cpp_device_shutdown, cpp_device_destroy};

unsafe extern "C" {
    fn device_read_block(dev: *mut c_void, lba: u64, count: u32, buffer: *mut u8) -> i32;
    fn device_write_block(dev: *mut c_void, lba: u64, count: u32, buffer: *const u8) -> i32;
    fn device_get_capacity(dev: *mut c_void) -> u64;
    fn device_set_capacity(dev: *mut c_void, capacity: u64);
    fn device_get_name(dev: *mut c_void) -> *const c_char;
    fn device_get_start_lba(dev: *mut c_void) -> u64;

    fn cpp_device_get_block_size(dev: *mut c_void) -> u32;
    fn cpp_create_partition_device(parent: *mut c_void, name: *const c_char, start: u64, end: u64, type_name: *const c_char);
    fn cpp_invalidate_device_cache(dev: *mut c_void);
    fn serial_write(s: *const c_char);
    fn printf(format: *const c_char, ...);

    fn device_unlock_write(dev: *mut c_void, magic: u32);
    fn device_lock_write(dev: *mut c_void);
    fn device_release(dev: *mut c_void);

    fn rust_free_string(ptr: *mut c_char);
}

const DEVICE_MAGIC_UNLOCK: u32 = 0x1337BEEF;

fn debug_print(msg: &str) {
    let c_str = alloc::ffi::CString::new(msg).unwrap();
    unsafe { serial_write(c_str.as_ptr()); }
}

fn user_print(msg: &str) {
    let c_str = alloc::ffi::CString::new(msg).unwrap();
    unsafe { printf(c_str.as_ptr()); }
}

const GPT_SIGNATURE: u64 = 0x5452415020494645;

unsafe fn read_u32(ptr: *const u8, offset: usize) -> u32 {
    unsafe {
        let src = ptr.add(offset);
        let mut bytes =[0u8; 4];
        ptr::copy_nonoverlapping(src, bytes.as_mut_ptr(), 4);
        u32::from_le_bytes(bytes)
    }
}

unsafe fn read_u64(ptr: *const u8, offset: usize) -> u64 {
    unsafe {
        let src = ptr.add(offset);
        let mut bytes =[0u8; 8];
        ptr::copy_nonoverlapping(src, bytes.as_mut_ptr(), 8);
        u64::from_le_bytes(bytes)
    }
}

unsafe fn write_u32(ptr: *mut u8, offset: usize, val: u32) {
    unsafe {
        let bytes = val.to_le_bytes();
        ptr::copy_nonoverlapping(bytes.as_ptr(), ptr.add(offset), 4);
    }
}

unsafe fn write_u64(ptr: *mut u8, offset: usize, val: u64) {
    unsafe {
        let bytes = val.to_le_bytes();
        ptr::copy_nonoverlapping(bytes.as_ptr(), ptr.add(offset), 8);
    }
}

unsafe fn write_bytes(ptr: *mut u8, offset: usize, data: &[u8]) {
    unsafe {
        ptr::copy_nonoverlapping(data.as_ptr(), ptr.add(offset), data.len());
    }
}

#[derive(Clone, Copy, Debug)]
struct GptHeaderLogic {
    signature: u64,
    revision: u32,
    header_size: u32,
    crc32_header: u32,
    reserved: u32,
    current_lba: u64,
    backup_lba: u64,
    first_usable_lba: u64,
    last_usable_lba: u64,
    disk_guid: [u8; 16],
    partition_entry_lba: u64,
    num_partition_entries: u32,
    size_partition_entry: u32,
    crc32_partition_array: u32,
}

impl GptHeaderLogic {
    unsafe fn from_buffer(buf: *const u8) -> Self {
        unsafe {
            let mut guid = [0u8; 16];
            ptr::copy_nonoverlapping(buf.add(56), guid.as_mut_ptr(), 16);
            Self {
                signature:             read_u64(buf, 0),
                revision:              read_u32(buf, 8),
                header_size:           read_u32(buf, 12),
                crc32_header:          read_u32(buf, 16),
                reserved:              read_u32(buf, 20),
                current_lba:           read_u64(buf, 24),
                backup_lba:            read_u64(buf, 32),
                first_usable_lba:      read_u64(buf, 40),
                last_usable_lba:       read_u64(buf, 48),
                disk_guid:             guid,
                partition_entry_lba:   read_u64(buf, 72),
                num_partition_entries: read_u32(buf, 80),
                size_partition_entry:  read_u32(buf, 84),
                crc32_partition_array: read_u32(buf, 88),
            }
        }
    }

    unsafe fn write_to_buffer(&self, buf: *mut u8) {
        unsafe {
            write_u64(buf, 0,  self.signature);
            write_u32(buf, 8,  self.revision);
            write_u32(buf, 12, self.header_size);
            write_u32(buf, 16, self.crc32_header);
            write_u32(buf, 20, self.reserved);
            write_u64(buf, 24, self.current_lba);
            write_u64(buf, 32, self.backup_lba);
            write_u64(buf, 40, self.first_usable_lba);
            write_u64(buf, 48, self.last_usable_lba);
            write_bytes(buf, 56, &self.disk_guid);
            write_u64(buf, 72, self.partition_entry_lba);
            write_u32(buf, 80, self.num_partition_entries);
            write_u32(buf, 84, self.size_partition_entry);
            write_u32(buf, 88, self.crc32_partition_array);
        }
    }
}

fn crc32(buf: &[u8]) -> u32 {
    let mut crc = 0xFFFF_FFFFu32;
    for &byte in buf {
        crc ^= byte as u32;
        for _ in 0..8 {
            if (crc & 1) != 0 { crc = (crc >> 1) ^ 0xEDB8_8320; }
            else               { crc >>= 1; }
        }
    }
    !crc
}

fn is_unused_guid(guid: &[u8; 16]) -> bool {
    guid.iter().all(|&x| x == 0)
}

fn get_guid_for_type(type_str: &str) -> [u8; 16] {
    if type_str == "linux" || type_str == "ext4" {[0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47, 0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4]
    } else if type_str == "efi" {[0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11, 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B]
    } else {[0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44, 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7]
    }
}

fn get_name_from_guid(guid: &[u8; 16]) -> &'static str {
    const DATA_GUID:  [u8; 16] =[0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44, 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7];
    const LINUX_GUID:[u8; 16] =[0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47, 0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4];
    const EFI_GUID:   [u8; 16] =[0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11, 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B];

    if *guid == DATA_GUID  { "Data"  }
    else if *guid == LINUX_GUID { "Linux" }
    else if *guid == EFI_GUID   { "EFI"   }
    else { "Unknown" }
}

fn generate_pseudo_guid() ->[u8; 16] {
    let mut guid = [0u8; 16];
    let tsc = crate::ffi::exports::get_system_ticks(); 
    let p1 = tsc.to_le_bytes();
    for i in 0..8 {
        guid[i]     = p1[i];
        guid[i + 8] = 0xAA;
    }
    guid
}

unsafe fn remove_old_partitions(parent_dev: *mut c_void) {
    let parent_name = unsafe {
        CStr::from_ptr(device_get_name(parent_dev)).to_str().unwrap_or("")
    };
    let prefix = format!("{}_p", parent_name);
    let mut to_remove = Vec::new();

    let path        = alloc::ffi::CString::new("/devices").unwrap();
    let mut idx     = 0u32;
    let mut name_buf = [0u8; 64];
    let mut obj_type: u8 = 0;

    while unsafe { ons_enumerate(path.as_ptr(), idx, name_buf.as_mut_ptr() as *mut c_char, &mut obj_type) } {
        idx += 1;
        let c_name = unsafe { CStr::from_ptr(name_buf.as_ptr() as *const c_char) };
        let p_name = c_name.to_str().unwrap_or("");

        if p_name.starts_with(&prefix) {
            let dev_ptr = unsafe { device_get(c_name.as_ptr()) };
            if !dev_ptr.is_null() && unsafe { cpp_device_get_type(dev_ptr) } == 5 {
                to_remove.push(dev_ptr);
            }
        }
    }

    for dev in to_remove {
        unsafe {
            cpp_device_shutdown(dev);
            cpp_device_destroy(dev);
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_gpt_scan_partitions(dev: *mut c_void) {
    unsafe {
        if dev.is_null() { return; }

        let bs = cpp_device_get_block_size(dev) as usize;
        if bs == 0 { return; }

        let disk_name_ptr = device_get_name(dev);
        let disk_name = if !disk_name_ptr.is_null() {
            CStr::from_ptr(disk_name_ptr).to_str().unwrap_or("disk")
        } else { "disk" };

        let mut sector_buf = vec![0u8; bs];
        if device_read_block(dev, 1, 1, sector_buf.as_mut_ptr()) == 0 { return; }

        let header = GptHeaderLogic::from_buffer(sector_buf.as_ptr());

        remove_old_partitions(dev);

        if header.signature == GPT_SIGNATURE {
            if device_get_capacity(dev) == 0 {
                device_set_capacity(dev, (header.backup_lba + 1) * (bs as u64));
            }

            let entry_count    = header.num_partition_entries as usize;
            let entry_size     = header.size_partition_entry as usize;
            let table_lba      = header.partition_entry_lba;
            let sectors_to_read = (entry_count * entry_size).div_ceil(bs);

            let mut table_buf = vec![0u8; sectors_to_read * bs];
            if device_read_block(dev, table_lba, sectors_to_read as u32, table_buf.as_mut_ptr()) == 0 { return; }

            let mut part_num = 1u32;

            for i in 0..entry_count {
                let offset    = i * entry_size;
                let entry_ptr = table_buf.as_ptr().add(offset);

                let mut type_guid = [0u8; 16];
                ptr::copy_nonoverlapping(entry_ptr, type_guid.as_mut_ptr(), 16);
                if is_unused_guid(&type_guid) { continue; }

                let start_lba = read_u64(entry_ptr, 32);
                let end_lba   = read_u64(entry_ptr, 40);
                let type_str  = get_name_from_guid(&type_guid);

                let part_name   = format!("{}_p{}", disk_name, part_num);
                let part_name_c = alloc::ffi::CString::new(part_name).unwrap().into_raw();
                let type_name_c = alloc::ffi::CString::new(type_str).unwrap().into_raw();

                cpp_create_partition_device(dev, part_name_c, start_lba, end_lba, type_name_c);

                rust_free_string(part_name_c);
                rust_free_string(type_name_c);

                part_num += 1;
            }
        } else {
            let msg = format!("[GPT] No GPT found on {}. Device is raw/unpartitioned.\n", disk_name);
            debug_print(&msg);
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_gpt_init_disk(dev_name: *const c_char) -> i32 {
    unsafe {
        let dev = device_get(dev_name);
        if dev.is_null() {
            user_print("Error: Device not found.\n");
            return 0;
        }

        let bs = cpp_device_get_block_size(dev) as usize;
        if bs == 0 { return 0; }

        let capacity     = device_get_capacity(dev);
        let total_sectors = capacity / (bs as u64);

        if total_sectors < 68 {
            user_print("Error: Disk is too small to hold a GPT structure.\n");
            return 0;
        }

        device_unlock_write(dev, DEVICE_MAGIC_UNLOCK);

        let mut mbr = vec![0u8; bs];
        mbr[446] = 0;
        mbr[447] = 0x00; mbr[448] = 0x02; mbr[449] = 0x00;
        mbr[450] = 0xEE;
        mbr[451] = 0xFF; mbr[452] = 0xFF; mbr[453] = 0xFF;
        mbr[454] = 1; mbr[455] = 0; mbr[456] = 0; mbr[457] = 0;

        let size_u32  = if total_sectors > 0xFFFF_FFFF { 0xFFFF_FFFFu32 } else { (total_sectors - 1) as u32 };
        mbr[458..462].copy_from_slice(&size_u32.to_le_bytes());
        mbr[510] = 0x55; mbr[511] = 0xAA;

        device_write_block(dev, 0, 1, mbr.as_ptr());

        let mut header = GptHeaderLogic {
            signature:             GPT_SIGNATURE,
            revision:              0x0001_0000,
            header_size:           92,
            crc32_header:          0,
            reserved:              0,
            current_lba:           1,
            backup_lba:            total_sectors - 1,
            first_usable_lba:      34,
            last_usable_lba:       total_sectors - 34,
            disk_guid:             generate_pseudo_guid(),
            partition_entry_lba:   2,
            num_partition_entries: 128,
            size_partition_entry:  128,
            crc32_partition_array: 0,
        };

        let array_size    = 128 * 128usize;
        let array_sectors = array_size.div_ceil(bs);
        let empty_array   = vec![0u8; array_sectors * bs];

        header.crc32_partition_array = crc32(&empty_array[0..array_size]);

        let mut header_buf = vec![0u8; bs];
        header.write_to_buffer(header_buf.as_mut_ptr());
        header.crc32_header = crc32(&header_buf[0..92]);
        header.write_to_buffer(header_buf.as_mut_ptr());

        device_write_block(dev, 1, 1, header_buf.as_ptr());
        device_write_block(dev, 2, array_sectors as u32, empty_array.as_ptr());

        header.current_lba         = total_sectors - 1;
        header.backup_lba          = 1;
        header.partition_entry_lba = total_sectors - 1 - (array_sectors as u64);
        header.crc32_header        = 0;
        header.write_to_buffer(header_buf.as_mut_ptr());
        header.crc32_header = crc32(&header_buf[0..92]);
        header.write_to_buffer(header_buf.as_mut_ptr());

        device_write_block(dev, header.partition_entry_lba, array_sectors as u32, empty_array.as_ptr());
        device_write_block(dev, header.current_lba, 1, header_buf.as_ptr());

        device_lock_write(dev);
        cpp_invalidate_device_cache(dev);
        rust_gpt_scan_partitions(dev);

        device_release(dev); 
        user_print("Disk initialized successfully.\n");
        1
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_gpt_create_partition(
    dev_name: *const c_char,
    size_mb: u32,
    name: *const c_char,
    type_str: *const c_char,
    out_created_name: *mut c_char,
) -> i32 {
    unsafe {
        let dev = device_get(dev_name);
        if dev.is_null() {
            user_print("Error: Device not found.\n");
            return 0;
        }

        let bs = cpp_device_get_block_size(dev) as usize;
        if bs == 0 { return 0; }

        let mut sector_buf = vec![0u8; bs];
        if device_read_block(dev, 1, 1, sector_buf.as_mut_ptr()) == 0 { return 0; }

        let mut header = GptHeaderLogic::from_buffer(sector_buf.as_ptr());
        if header.signature != GPT_SIGNATURE {
            user_print("Error: No GPT found on this disk. Run 'fdisk init <disk>' first!\n");
            return 0;
        }

        let entry_count     = header.num_partition_entries as usize;
        let entry_size      = header.size_partition_entry as usize;
        let table_lba       = header.partition_entry_lba;
        let total_size      = entry_count * entry_size;
        let sectors_to_read = total_size.div_ceil(bs);

        let mut table_buf = vec![0u8; sectors_to_read * bs];
        if device_read_block(dev, table_lba, sectors_to_read as u32, table_buf.as_mut_ptr()) == 0 { return 0; }

        let mut free_index  = None;
        let mut max_end_lba = header.first_usable_lba - 1;

        for i in 0..entry_count {
            let offset    = i * entry_size;
            let entry_ptr = table_buf.as_ptr().add(offset);
            let mut type_guid = [0u8; 16];
            ptr::copy_nonoverlapping(entry_ptr, type_guid.as_mut_ptr(), 16);
            let end_lba = read_u64(entry_ptr, 40);

            if is_unused_guid(&type_guid) {
                if free_index.is_none() { free_index = Some(i); }
            } else if end_lba > max_end_lba {
                max_end_lba = end_lba;
            }
        }

        let idx = match free_index {
            Some(i) => i,
            None    => { user_print("Error: No free partition entries left in GPT table.\n"); return 0; },
        };

        let mut start_lba  = max_end_lba + 1;
        let align_blocks   = (1024 * 1024) / bs as u64;
        if !start_lba.is_multiple_of(align_blocks) {
            start_lba += align_blocks - (start_lba % align_blocks);
        }

        let sector_count = (size_mb as u64 * 1024 * 1024) / (bs as u64);
        let end_lba      = start_lba + sector_count - 1;

        if end_lba > header.last_usable_lba {
            user_print("Error: Not enough unallocated space on disk for this partition.\n");
            return 0;
        }

        let offset    = idx * entry_size;
        let entry_ptr = table_buf.as_mut_ptr().add(offset);

        let type_cstr  = CStr::from_ptr(type_str);
        let type_guid  = get_guid_for_type(type_cstr.to_str().unwrap_or("data"));
        let unique_guid = generate_pseudo_guid();

        write_bytes(entry_ptr, 0,  &type_guid);
        write_bytes(entry_ptr, 16, &unique_guid);
        write_u64(entry_ptr, 32, start_lba);
        write_u64(entry_ptr, 40, end_lba);
        write_u64(entry_ptr, 48, 0);
        ptr::write_bytes(entry_ptr.add(56), 0, 72);

        let name_cstr = CStr::from_ptr(name);
        let name_str  = name_cstr.to_str().unwrap_or("part");
        let mut name_offset = 56usize;
        for (i, c) in name_str.chars().enumerate() {
            if i >= 35 { break; }
            let b = (c as u16).to_le_bytes();
            ptr::copy_nonoverlapping(b.as_ptr(), entry_ptr.add(name_offset), 2);
            name_offset += 2;
        }

        let part_crc = crc32(&table_buf[0..total_size]);
        header.crc32_partition_array = part_crc;
        header.crc32_header = 0;
        let mut header_bytes = vec![0u8; 92];
        header.write_to_buffer(header_bytes.as_mut_ptr());
        header.crc32_header = crc32(&header_bytes);

        device_unlock_write(dev, DEVICE_MAGIC_UNLOCK);

        let mut success      = true;
        let mut header_sector = vec![0u8; bs];

        if device_write_block(dev, table_lba, sectors_to_read as u32, table_buf.as_ptr()) == 0 { success = false; }
        if success {
            header.write_to_buffer(header_sector.as_mut_ptr());
            if device_write_block(dev, 1, 1, header_sector.as_ptr()) == 0 { success = false; }
        }
        if success {
            let mut backup_header = header;
            backup_header.current_lba         = header.backup_lba;
            backup_header.backup_lba          = header.current_lba;
            backup_header.partition_entry_lba = (header.backup_lba).saturating_sub(sectors_to_read as u64);
            backup_header.crc32_header        = 0;
            backup_header.write_to_buffer(header_bytes.as_mut_ptr());
            backup_header.crc32_header = crc32(&header_bytes);
            
            if device_write_block(dev, backup_header.partition_entry_lba, sectors_to_read as u32, table_buf.as_ptr()) == 0 { success = false; }
            if success {
                ptr::write_bytes(header_sector.as_mut_ptr(), 0, bs);
                backup_header.write_to_buffer(header_sector.as_mut_ptr());
                if device_write_block(dev, backup_header.current_lba, 1, header_sector.as_ptr()) == 0 { success = false; }
            }
        }

        device_lock_write(dev);

        if !success {
            device_release(dev); 
            user_print("Error: Disk write failure during GPT update. Is the device read-only?\n");
            return 0;
        }

        cpp_invalidate_device_cache(dev);

        if !out_created_name.is_null() {
            let mut part_cnt = 0u32;
            for i in 0..entry_count {
                let off = i * entry_size;
                let ep  = table_buf.as_ptr().add(off);
                let mut tg = [0u8; 16];
                ptr::copy_nonoverlapping(ep, tg.as_mut_ptr(), 16);
                if !is_unused_guid(&tg) { part_cnt += 1; }
            }
            let dev_name_str = CStr::from_ptr(dev_name).to_str().unwrap_or("disk");
            let out_str      = format!("{}_p{}", dev_name_str, part_cnt);
            let out_bytes    = out_str.as_bytes();
            ptr::copy_nonoverlapping(out_bytes.as_ptr(), out_created_name as *mut u8, out_bytes.len());
            *out_created_name.add(out_bytes.len()) = 0;
        }

        rust_gpt_scan_partitions(dev);
        
        device_release(dev); 
        1
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_gpt_delete_partition(part_name: *const c_char) -> i32 {
    unsafe {
        let part_dev = device_get(part_name);
        if part_dev.is_null() {
            user_print("Error: Partition not found.\n");
            return 0;
        }

        let part_str = CStr::from_ptr(part_name).to_str().unwrap_or("");
        let parts: Vec<&str> = part_str.split("_p").collect();
        if parts.len() < 2 { return 0; }

        let parent_name = alloc::ffi::CString::new(parts[0]).unwrap();
        let parent_dev  = device_get(parent_name.as_ptr());
        if parent_dev.is_null() { return 0; }

        let bs = cpp_device_get_block_size(parent_dev) as usize;
        if bs == 0 { return 0; }

        let target_start = device_get_start_lba(part_dev);

        let mut sector_buf = vec![0u8; bs];
        if device_read_block(parent_dev, 1, 1, sector_buf.as_mut_ptr()) == 0 { return 0; }

        let mut header = GptHeaderLogic::from_buffer(sector_buf.as_ptr());
        if header.signature != GPT_SIGNATURE { return 0; }

        let entry_count     = header.num_partition_entries as usize;
        let entry_size      = header.size_partition_entry as usize;
        let table_lba       = header.partition_entry_lba;
        let total_size      = entry_count * entry_size;
        let sectors_to_read = total_size.div_ceil(bs);

        let mut table_buf = vec![0u8; sectors_to_read * bs];
        if device_read_block(parent_dev, table_lba, sectors_to_read as u32, table_buf.as_mut_ptr()) == 0 { return 0; }

        let mut found = false;
        for i in 0..entry_count {
            let offset    = i * entry_size;
            let entry_ptr = table_buf.as_mut_ptr().add(offset);
            let mut type_guid =[0u8; 16];
            ptr::copy_nonoverlapping(entry_ptr, type_guid.as_mut_ptr(), 16);
            let start_lba = read_u64(entry_ptr, 32);

            if !is_unused_guid(&type_guid) && start_lba == target_start {
                ptr::write_bytes(entry_ptr, 0, entry_size);
                found = true;
                break;
            }
        }

        if !found {
            user_print("Error: Could not locate partition entry in GPT table.\n");
            return 0;
        }

        let part_crc = crc32(&table_buf[0..total_size]);
        header.crc32_partition_array = part_crc;
        header.crc32_header = 0;
        let mut header_bytes = vec![0u8; 92];
        header.write_to_buffer(header_bytes.as_mut_ptr());
        header.crc32_header = crc32(&header_bytes);

        device_unlock_write(parent_dev, DEVICE_MAGIC_UNLOCK);
        let mut success      = true;
        let mut header_sector = vec![0u8; bs];

        if device_write_block(parent_dev, table_lba, sectors_to_read as u32, table_buf.as_ptr()) == 0 { success = false; }
        if success {
            header.write_to_buffer(header_sector.as_mut_ptr());
            if device_write_block(parent_dev, 1, 1, header_sector.as_ptr()) == 0 { success = false; }
        }
        if success {
            let mut backup_header = header;
            backup_header.current_lba         = header.backup_lba;
            backup_header.backup_lba          = header.current_lba;
            backup_header.partition_entry_lba = (header.backup_lba).saturating_sub(sectors_to_read as u64);
            backup_header.crc32_header        = 0;
            backup_header.write_to_buffer(header_bytes.as_mut_ptr());
            backup_header.crc32_header = crc32(&header_bytes);

            if device_write_block(parent_dev, backup_header.partition_entry_lba, sectors_to_read as u32, table_buf.as_ptr()) == 0 { success = false; }
            if success {
                ptr::write_bytes(header_sector.as_mut_ptr(), 0, bs);
                backup_header.write_to_buffer(header_sector.as_mut_ptr());
                if device_write_block(parent_dev, backup_header.current_lba, 1, header_sector.as_ptr()) == 0 { success = false; }
            }
        }

        device_lock_write(parent_dev);

        if !success {
            user_print("Error: Disk write failure during GPT deletion.\n");
            return 0;
        }

        cpp_invalidate_device_cache(parent_dev);
        rust_gpt_scan_partitions(parent_dev);

        device_release(parent_dev); 
        user_print("Partition successfully deleted.\n");
        1
    }
}
