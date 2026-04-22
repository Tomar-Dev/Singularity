// rust_core/src/kom/device.rs

use alloc::string::String;
use alloc::string::ToString;
use alloc::vec::Vec;
use alloc::format;
use crate::arch::sync::IrqSpinlock;
use core::ffi::{c_void, c_char, CStr};

const CONSOLE_COLOR_BLACK:       u8 = 0;
const CONSOLE_COLOR_LIGHT_GREY:  u8 = 7;
const CONSOLE_COLOR_DARK_GREY:   u8 = 8;
const CONSOLE_COLOR_LIGHT_GREEN: u8 = 10;
const CONSOLE_COLOR_LIGHT_CYAN:  u8 = 11;
const CONSOLE_COLOR_LIGHT_RED:   u8 = 12;
const CONSOLE_COLOR_LIGHT_MAGENTA: u8 = 13;
const CONSOLE_COLOR_YELLOW:      u8 = 14;
const CONSOLE_COLOR_WHITE:       u8 = 15;

unsafe extern "C" {
    fn get_secure_random() -> u64;
    fn kprintf_string(s: *const i8);
    
    pub fn ons_enumerate(path: *const c_char, index: u32, out_name: *mut c_char, out_type: *mut u8) -> bool;
    pub fn device_get(name: *const c_char) -> *mut c_void;
    pub fn cpp_device_get_type(dev: *mut c_void) -> u32;
    pub fn cpp_device_shutdown(dev: *mut c_void);
    pub fn cpp_device_destroy(dev: *mut c_void);
    pub fn cpp_device_get_name(dev: *mut c_void) -> *const c_char;
    pub fn cpp_device_get_capacity(dev: *mut c_void) -> u64;
    pub fn cpp_device_get_model(dev: *mut c_void) -> *const c_char;
    pub fn cpp_device_is_read_only(dev: *mut c_void) -> bool;
    pub fn cpp_device_get_block_size(dev: *mut c_void) -> u32;
    pub fn cpp_device_read_block(dev: *mut c_void, lba: u64, count: u32, buf: *mut u8) -> i32;
    pub fn cpp_partition_get_gpt_type(dev: *mut c_void) -> *const c_char;
    pub fn cpp_device_get_free_space(dev: *mut c_void) -> u64;
    pub fn console_set_color(fg: u8, bg: u8); 
    pub fn device_release(dev: *mut c_void);
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum DeviceClass {
    Unknown = 0,
    Block,
    Character,
    Network,
    Gpu,
    Bus,
}

pub struct DeviceNode {
    pub name: String,
    pub class: DeviceClass,
    pub token: u64,
    pub block_size: u32,
    pub total_blocks: u64,
    pub native_ptr: *mut c_void, 
}

unsafe impl Send for DeviceNode {}
unsafe impl Sync for DeviceNode {}

pub struct DeviceTree {
    pub devices: Vec<DeviceNode>,
    pub initialized: bool,
}

pub static DEVICE_TREE: IrqSpinlock<DeviceTree> = IrqSpinlock::new(DeviceTree {
    devices: Vec::new(),
    initialized: false,
});

pub fn register_device(name: &str, class: DeviceClass, block_size: u32, total_blocks: u64, native_ptr: *mut c_void) -> u64 {
    let token = unsafe { get_secure_random() };
    let node = DeviceNode {
        name: String::from(name),
        class,
        token,
        block_size,
        total_blocks,
        native_ptr,
    };
    
    let mut tree = DEVICE_TREE.lock();
    tree.devices.push(node);
    
    token
}

pub fn register_device_silent(name: &str, class: DeviceClass, block_size: u32, total_blocks: u64, native_ptr: *mut c_void) -> u64 {
    let token = unsafe { get_secure_random() };
    let node = DeviceNode {
        name: String::from(name),
        class,
        token,
        block_size,
        total_blocks,
        native_ptr,
    };
    
    let mut tree = DEVICE_TREE.lock();
    tree.devices.push(node);
    
    token
}

fn format_size_dec(bytes: u64) -> String {
    if bytes == u64::MAX {
        return String::from("Unknown");
    } else if bytes == 0 { 
        return String::from("0 B"); 
    } else if bytes >= 1024 * 1024 * 1024 {
        let gb = bytes / (1024 * 1024 * 1024);
        let mb_rem = ((bytes % (1024 * 1024 * 1024)) * 10) / (1024 * 1024 * 1024);
        return format!("{}.{} GB", gb, mb_rem);
    } else if bytes >= 1024 * 1024 {
        let mb = bytes / (1024 * 1024);
        let kb_rem = ((bytes % (1024 * 1024)) * 10) / (1024 * 1024);
        return format!("{}.{} MB", mb, kb_rem);
    } else {
        let kb = bytes / 1024;
        return format!("{}.0 KB", kb);
    }
}

fn print_c(s: &str, fg: u8) {
    unsafe {
        console_set_color(fg, CONSOLE_COLOR_BLACK);
        let c_msg = format!("{}\0", s);
        kprintf_string(c_msg.as_ptr() as *const i8);
        console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
    }
}

fn pad_right(s: &str, width: usize) -> String {
    let len = s.chars().count();
    if len >= width { 
        s.to_string() 
    } else { 
        format!("{}{}", s, " ".repeat(width - len)) 
    }
}

fn print_tree_prefix(is_last: bool) {
    let prefix = if is_last { " \\- " } else { " |- " };
    unsafe {
        console_set_color(CONSOLE_COLOR_DARK_GREY, CONSOLE_COLOR_BLACK);
        let c_msg = format!("{}\0", prefix);
        kprintf_string(c_msg.as_ptr() as *const i8);
        console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_device_detect_fs(dev: *mut c_void) -> u32 {
    let bs = unsafe { cpp_device_get_block_size(dev) } as usize;
    let mut fs = 0; 

    if bs > 0 {
        let mut buf = alloc::vec![0u8; bs.max(2048)];
        
        if fs == 0 && bs >= 2048 {
            if unsafe { cpp_device_read_block(dev, 256, 1, buf.as_mut_ptr()) } == 1 {
                let tag_id = u16::from_le_bytes([buf[0], buf[1]]);
                if tag_id == 2 {
                    let mut sum: u8 = 0;
                    for i in 0..16 {
                        if i != 4 { 
                            sum = sum.wrapping_add(buf[i]); 
                        }
                    }
                    if sum == buf[4] {
                        fs = 4; 
                    }
                }
            }
            
            if fs == 0 && bs == 2048 {
                if unsafe { cpp_device_read_block(dev, 16, 1, buf.as_mut_ptr()) } == 1 {
                    if buf[1] == b'C' && buf[2] == b'D' && buf[3] == b'0' && buf[4] == b'0' && buf[5] == b'1' {
                        fs = 3; 
                    }
                }
            }
        }

        if fs == 0 && bs == 512 {
            if unsafe { cpp_device_read_block(dev, 2, 2, buf.as_mut_ptr()) } == 1 {
                let ext_magic = u16::from_le_bytes([buf[56], buf[57]]); 
                if ext_magic == 0xEF53 { 
                    fs = 2; 
                }
            }
            
            if fs == 0 {
                let mut boot_buf =[0u8; 512];
                if unsafe { cpp_device_read_block(dev, 0, 1, boot_buf.as_mut_ptr()) } == 1 {
                    if boot_buf[510] == 0x55 && boot_buf[511] == 0xAA {
                        if &boot_buf[82..87] == b"FAT32" { 
                            fs = 1; 
                        }
                    }
                }
            }
        }
    } else {
        fs = 5; 
    }

    fs
}

struct DiskInfo {
    name: String,
    capacity: u64,
    model: String,
    is_ro: bool,
    is_optical: bool,
}

struct PartInfo {
    name: String,
    capacity: u64,
    fs_val: u32,
    free_space: u64,
    is_ro: bool,
}

#[unsafe(no_mangle)]
pub extern "C" fn rust_device_print_disks() {
    print_c("========================================================================================================================\n", CONSOLE_COLOR_LIGHT_CYAN);
    print_c(&pad_right(" NAME", 20), CONSOLE_COLOR_WHITE);
    print_c(&pad_right("| TYPE / FS", 15), CONSOLE_COLOR_WHITE);
    print_c(&pad_right("| CAPACITY", 20), CONSOLE_COLOR_WHITE);
    print_c(&pad_right("| FREE SPACE", 20), CONSOLE_COLOR_WHITE);
    print_c(&pad_right("| R/W", 8), CONSOLE_COLOR_WHITE);
    print_c("| MODEL\n", CONSOLE_COLOR_WHITE);
    print_c("------------------------------------------------------------------------------------------------------------------------\n", CONSOLE_COLOR_DARK_GREY);

    let path = c"/devices";
    let mut idx = 0u32;
    let mut name_buf =[0i8; 64];
    let mut obj_type = 0u8;

    let mut base_disks = Vec::new();

    // 1. Collect all base disks (non-partitions)
    while unsafe { ons_enumerate(path.as_ptr(), idx, name_buf.as_mut_ptr(), &mut obj_type) } {
        idx += 1;
        let dev_ptr = unsafe { device_get(name_buf.as_ptr()) };
        if !dev_ptr.is_null() && unsafe { cpp_device_get_type(dev_ptr) } == 4 { 
            let d_name = unsafe { CStr::from_ptr(cpp_device_get_name(dev_ptr)).to_str().unwrap_or("?") }.to_string();
            
            // If the name contains "_p", it is a partition, not a base disk.
            if !d_name.contains("_p") {
                let d_cap = unsafe { cpp_device_get_capacity(dev_ptr) };
                let d_model = unsafe { CStr::from_ptr(cpp_device_get_model(dev_ptr)).to_str().unwrap_or("Generic") }.to_string();
                let is_ro = unsafe { cpp_device_is_read_only(dev_ptr) };
                let fs_val = unsafe { rust_device_detect_fs(dev_ptr) };
                
                let is_optical = fs_val == 3 || fs_val == 4 || d_name.starts_with("cdrom");
                
                base_disks.push(DiskInfo {
                    name: d_name,
                    capacity: d_cap,
                    model: d_model,
                    is_ro,
                    is_optical,
                });
            }
        }
        if !dev_ptr.is_null() {
            unsafe { device_release(dev_ptr); }
        }
    }

    // Sort by name (cdrom0, disk0, nvme0, etc.)
    base_disks.sort_by(|a, b| a.name.cmp(&b.name));

    // 2. Find partitions for each base disk and print in Tree format
    for disk in base_disks.iter() {
        let mut partitions = Vec::new();
        
        // Optical media do not have partitions, skip.
        if !disk.is_optical {
            let mut p_idx = 0u32;
            let mut p_name_buf =[0i8; 64];
            let mut p_type = 0u8;
            let prefix = format!("{}_p", disk.name);

            while unsafe { ons_enumerate(path.as_ptr(), p_idx, p_name_buf.as_mut_ptr(), &mut p_type) } {
                p_idx += 1;
                let p_name = unsafe { CStr::from_ptr(p_name_buf.as_ptr()).to_str().unwrap_or("") };
                if p_name.starts_with(&prefix) {
                    let part_ptr = unsafe { device_get(p_name_buf.as_ptr()) };
                    if !part_ptr.is_null() {
                        let p_cap = unsafe { cpp_device_get_capacity(part_ptr) };
                        let fs_val = unsafe { rust_device_detect_fs(part_ptr) };
                        let d_free = unsafe { cpp_device_get_free_space(part_ptr) };
                        let is_ro = unsafe { cpp_device_is_read_only(part_ptr) };

                        partitions.push(PartInfo {
                            name: p_name.to_string(),
                            capacity: p_cap,
                            fs_val,
                            free_space: d_free,
                            is_ro,
                        });
                        unsafe { device_release(part_ptr); }
                    }
                }
            }
            partitions.sort_by(|a, b| a.name.cmp(&b.name));
        }

        // Calculate base disk free space (Capacity - Sum of Partitions)
        let mut used_capacity = 0u64;
        for p in partitions.iter() {
            used_capacity += p.capacity;
        }
        
        let disk_free = if disk.is_optical { 0 } else { disk.capacity.saturating_sub(used_capacity) };
        let disk_free_pct_x10 = if disk.capacity > 0 { (disk_free * 1000) / disk.capacity } else { 0 };
        let disk_free_str = if disk.is_optical { 
            "0 B (0.0%)".to_string() 
        } else { 
            format!("{} ({}.{}%)", format_size_dec(disk_free), disk_free_pct_x10 / 10, disk_free_pct_x10 % 10) 
        };

        // Base Disk Row
        let name_color = if disk.is_optical { CONSOLE_COLOR_LIGHT_MAGENTA } else { CONSOLE_COLOR_LIGHT_GREEN };
        let type_str = if disk.is_optical { "Optical" } else { "Disk" };
        
        print_c(&pad_right(&format!(" {}", disk.name), 20), name_color);
        print_c(&pad_right(&format!("| {}", type_str), 15), CONSOLE_COLOR_WHITE);
        print_c(&pad_right(&format!("| {}", format_size_dec(disk.capacity)), 20), CONSOLE_COLOR_YELLOW);
        print_c(&pad_right(&format!("| {}", disk_free_str), 20), CONSOLE_COLOR_LIGHT_GREEN);
        
        print_c("| ", CONSOLE_COLOR_WHITE);
        if disk.is_ro { 
            print_c(&pad_right("R/O", 6), CONSOLE_COLOR_LIGHT_RED); 
        } else { 
            print_c(&pad_right("R/W", 6), CONSOLE_COLOR_LIGHT_GREEN); 
        }
        
        print_c(&format!("| {}\n", disk.model), CONSOLE_COLOR_LIGHT_GREY);

        // Print partitions
        let total_children = partitions.len();
        for (i, part) in partitions.iter().enumerate() {
            let is_last = i == total_children - 1;
            
            let fs_name = match part.fs_val {
                1 => "fat32",
                2 => "ext4",
                3 => "iso9660",
                4 => "udf",
                _ => "raw",
            };

            print_tree_prefix(is_last);
            print_c(&pad_right(&format!("{}", part.name), 16), CONSOLE_COLOR_LIGHT_CYAN);
            print_c(&pad_right(&format!("| {}", fs_name), 15), CONSOLE_COLOR_WHITE);
            
            // Partition to disk ratio
            let part_cap_pct_x10 = if disk.capacity > 0 { (part.capacity * 1000) / disk.capacity } else { 0 };
            let part_cap_str = format!("{} ({}.{}%)", format_size_dec(part.capacity), part_cap_pct_x10 / 10, part_cap_pct_x10 % 10);
            print_c(&pad_right(&format!("| {}", part_cap_str), 20), CONSOLE_COLOR_YELLOW);
            
            // Partition's internal free space
            let free_str = if part.free_space == u64::MAX { 
                "Unknown".to_string() 
            } else { 
                let pct_x10 = if part.capacity > 0 { (part.free_space * 1000) / part.capacity } else { 0 };
                format!("{} ({}.{}%)", format_size_dec(part.free_space), pct_x10 / 10, pct_x10 % 10)
            };
            print_c(&pad_right(&format!("| {}", free_str), 20), CONSOLE_COLOR_LIGHT_GREEN);
            
            print_c("| ", CONSOLE_COLOR_WHITE);
            if part.is_ro { 
                print_c(&pad_right("R/O", 6), CONSOLE_COLOR_LIGHT_RED); 
            } else { 
                print_c(&pad_right("R/W", 6), CONSOLE_COLOR_LIGHT_GREEN); 
            }
            print_c("| -\n", CONSOLE_COLOR_DARK_GREY);
        }
    }

    print_c("========================================================================================================================\n", CONSOLE_COLOR_LIGHT_CYAN);
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_register_device(
    name_ptr: *const c_char,
    class_id: u8,
    block_size: u32,
    total_blocks: u64,
    native_ptr: *mut c_void,
) -> u64 {
    if name_ptr.is_null() {
        return 0;
    } else {
        let name_cstr = unsafe { CStr::from_ptr(name_ptr) };
        let name_str = name_cstr.to_str().unwrap_or("unknown_device");
        
        let class = match class_id {
            1 => DeviceClass::Block,
            2 => DeviceClass::Character,
            3 => DeviceClass::Network,
            4 => DeviceClass::Gpu,
            5 => DeviceClass::Bus,
            _ => DeviceClass::Unknown,
        };
        
        return register_device(name_str, class, block_size, total_blocks, native_ptr);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn rust_unregister_device(token: u64) -> i32 {
    let mut tree = DEVICE_TREE.lock();
    if let Some(pos) = tree.devices.iter().position(|d| d.token == token) {
        tree.devices.remove(pos);
        return 1;
    } else {
        return 0;
    }
}