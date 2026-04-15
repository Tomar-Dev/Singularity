// rust_core/src/kom/device.rs

use alloc::string::String;
use alloc::string::ToString;
use alloc::vec::Vec;
use alloc::format;
use crate::arch::sync::IrqSpinlock;
use core::ffi::{c_void, c_char, CStr};

const VGA_BLACK: u8       = 0;
const VGA_LIGHT_GREY: u8  = 7;
const VGA_DARK_GREY: u8   = 8;
const VGA_LIGHT_GREEN: u8 = 10;
const VGA_LIGHT_CYAN: u8  = 11;
const VGA_LIGHT_RED: u8   = 12;
const VGA_YELLOW: u8      = 14;
const VGA_WHITE: u8       = 15;

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
    pub fn vga_set_color(fg: u8, bg: u8);
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
    if bytes == 0 || bytes == u64::MAX { 
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
        vga_set_color(fg, VGA_BLACK);
        let c_msg = format!("{}\0", s);
        kprintf_string(c_msg.as_ptr() as *const i8);
        vga_set_color(VGA_WHITE, VGA_BLACK);
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

// OPTİMİZASYON YAMASI: FFI üzerinden string allocation (tahsis) işlemleri tamamen
// iptal edilerek, maliyetsiz bir integer (enum) döndüren modele geçildi.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_device_detect_fs(dev: *mut c_void) -> u32 {
    let bs = unsafe { cpp_device_get_block_size(dev) } as usize;
    let mut fs = 0; // 0 = raw

    if bs > 0 {
        let mut buf = alloc::vec![0u8; bs.max(2048)];
        
        if fs == 0 && bs >= 2048 {
            if unsafe { cpp_device_read_block(dev, 256, 1, buf.as_mut_ptr()) } == 1 {
                let tag_id = u16::from_le_bytes([buf[0], buf[1]]);
                if tag_id == 2 {
                    let mut sum: u8 = 0;
                    for i in 0..16 {
                        if i != 4 { sum = sum.wrapping_add(buf[i]); }
                    }
                    if sum == buf[4] {
                        fs = 4; // udf
                    }
                }
            }
            
            if fs == 0 && bs == 2048 {
                if unsafe { cpp_device_read_block(dev, 16, 1, buf.as_mut_ptr()) } == 1
                    && buf[1] == b'C' && buf[2] == b'D' && buf[3] == b'0'
                    && buf[4] == b'0' && buf[5] == b'1'
                { fs = 3; /* iso9660 */ }
            }
        }

        if fs == 0 && bs == 512 {
            if unsafe { cpp_device_read_block(dev, 2, 2, buf.as_mut_ptr()) } == 1 {
                let ext_magic = u16::from_le_bytes([buf[56], buf[57]]); 
                if ext_magic == 0xEF53 { fs = 2; /* ext4 */ } else {}
            } else {}
            
            if fs == 0 {
                let mut boot_buf =[0u8; 512];
                if unsafe { cpp_device_read_block(dev, 0, 1, boot_buf.as_mut_ptr()) } == 1 {
                    if boot_buf[510] == 0x55 && boot_buf[511] == 0xAA {
                        if &boot_buf[82..87] == b"FAT32" { fs = 1; /* fat32 */ } else {}
                    } else {}
                } else {}
            } else {}
        } else {}
    } else {
        fs = 5; // Unknown
    }

    fs
}

#[unsafe(no_mangle)]
pub extern "C" fn rust_device_print_disks() {
    print_c("========================================================================================================================\n", VGA_LIGHT_CYAN);
    print_c(&pad_right(" DISK NAME", 15), VGA_WHITE);
    print_c(&pad_right("| TYPE", 20), VGA_WHITE);
    print_c(&pad_right("| CAPACITY", 15), VGA_WHITE);
    print_c(&pad_right("| R/W", 8), VGA_WHITE);
    print_c(&pad_right("| PARTS", 10), VGA_WHITE);
    print_c("| MODEL\n", VGA_WHITE);
    print_c("------------------------------------------------------------------------------------------------------------------------\n", VGA_DARK_GREY);

    let path = c"/devices";
    let mut idx = 0u32;
    let mut name_buf =[0i8; 64];
    let mut obj_type = 0u8;

    while unsafe { ons_enumerate(path.as_ptr(), idx, name_buf.as_mut_ptr(), &mut obj_type) } {
        idx += 1;
        let dev_ptr = unsafe { device_get(name_buf.as_ptr()) };
        if dev_ptr.is_null() { continue; } else {
            if unsafe { cpp_device_get_type(dev_ptr) } != 4 { 
                unsafe { device_release(dev_ptr); }
                continue; 
            } else {
                let d_name = unsafe { CStr::from_ptr(cpp_device_get_name(dev_ptr)).to_str().unwrap_or("?") };
                let d_cap = unsafe { cpp_device_get_capacity(dev_ptr) };
                let d_model = unsafe { CStr::from_ptr(cpp_device_get_model(dev_ptr)).to_str().unwrap_or("Generic") };
                let is_ro = unsafe { cpp_device_is_read_only(dev_ptr) };

                let mut p_count = 0;
                let mut p_idx = 0u32;
                let mut p_name_buf = [0i8; 64];
                let mut p_type = 0u8;
                let prefix = format!("{}_p", d_name);
                while unsafe { ons_enumerate(path.as_ptr(), p_idx, p_name_buf.as_mut_ptr(), &mut p_type) } {
                    p_idx += 1;
                    let p_name = unsafe { CStr::from_ptr(p_name_buf.as_ptr()).to_str().unwrap_or("") };
                    if p_name.starts_with(&prefix) { p_count += 1; } else {}
                }

                print_c(&pad_right(&format!(" {}", d_name), 15), VGA_LIGHT_GREEN);
                print_c(&pad_right("| Block Storage", 20), VGA_WHITE);
                print_c(&pad_right(&format!("| {}", format_size_dec(d_cap)), 15), VGA_YELLOW);
                print_c("| ", VGA_WHITE);
                if is_ro { print_c(&pad_right("R/O", 6), VGA_LIGHT_RED); } else { print_c(&pad_right("R/W", 6), VGA_LIGHT_GREEN); }
                print_c(&pad_right(&format!("| {}", p_count), 10), VGA_LIGHT_CYAN);
                print_c(&format!("| {}\n", d_model), VGA_LIGHT_GREY);
                
                unsafe { device_release(dev_ptr); }
            }
        }
    }
    print_c("========================================================================================================================\n", VGA_LIGHT_CYAN);
}

#[unsafe(no_mangle)]
pub extern "C" fn rust_device_print_parts() {
    print_c("========================================================================================================================\n", VGA_LIGHT_CYAN);
    print_c(&pad_right(" PARTITION", 15), VGA_WHITE);
    print_c(&pad_right("| DISK", 15), VGA_WHITE);
    print_c(&pad_right("| FILE SYSTEM", 15), VGA_WHITE);
    print_c(&pad_right("| CAPACITY", 15), VGA_WHITE);
    print_c("| FREE SPACE\n", VGA_WHITE);
    print_c("------------------------------------------------------------------------------------------------------------------------\n", VGA_DARK_GREY);

    let path = c"/devices";
    let mut idx = 0u32;
    let mut name_buf =[0i8; 64];
    let mut obj_type = 0u8;

    let mut disk_list = Vec::new();
    while unsafe { ons_enumerate(path.as_ptr(), idx, name_buf.as_mut_ptr(), &mut obj_type) } {
        idx += 1;
        let dev_ptr = unsafe { device_get(name_buf.as_ptr()) };
        if !dev_ptr.is_null() && unsafe { cpp_device_get_type(dev_ptr) } == 4 { 
            disk_list.push(dev_ptr);
        } else if !dev_ptr.is_null() {
            unsafe { device_release(dev_ptr); }
        } else {}
    }

    for &disk_ptr in disk_list.iter() {
        let d_name = unsafe { CStr::from_ptr(cpp_device_get_name(disk_ptr)).to_str().unwrap_or("?") };
        let d_total_cap = unsafe { cpp_device_get_capacity(disk_ptr) };
        let mut d_used_cap = 0u64;

        let mut p_idx = 0u32;
        let mut p_name_buf =[0i8; 64];
        let mut p_type = 0u8;
        let prefix = format!("{}_p", d_name);

        while unsafe { ons_enumerate(path.as_ptr(), p_idx, p_name_buf.as_mut_ptr(), &mut p_type) } {
            p_idx += 1;
            let p_name = unsafe { CStr::from_ptr(p_name_buf.as_ptr()).to_str().unwrap_or("") };
            if p_name.starts_with(&prefix) {
                let part_ptr = unsafe { device_get(p_name_buf.as_ptr()) };
                if !part_ptr.is_null() {
                    let p_cap = unsafe { cpp_device_get_capacity(part_ptr) };
                    d_used_cap += p_cap;

                    // MİMARİ YAMASI: FFI Integer üzerinden dosya sistemi çözümleme
                    let fs_val = unsafe { rust_device_detect_fs(part_ptr) };
                    let fs_name = match fs_val {
                        1 => "fat32",
                        2 => "ext4",
                        3 => "iso9660",
                        4 => "udf",
                        _ => "raw",
                    };
                    
                    let d_free = unsafe { cpp_device_get_free_space(part_ptr) };

                    print_c(&pad_right(&format!(" {}", p_name), 15), VGA_LIGHT_CYAN);
                    print_c(&pad_right(&format!("| {}", d_name), 15), VGA_WHITE);
                    print_c(&pad_right(&format!("| {}", fs_name), 15), VGA_WHITE);
                    print_c(&pad_right(&format!("| {}", format_size_dec(p_cap)), 15), VGA_YELLOW);
                    
                    let free_str = if d_free == u64::MAX { "Unknown".to_string() } else { format_size_dec(d_free) };
                    print_c(&format!("| {}\n", free_str), VGA_LIGHT_GREEN);

                    unsafe { device_release(part_ptr); }
                } else {}
            } else {}
        }

        if d_total_cap > d_used_cap + (1024 * 1024) {
            let unalloc = d_total_cap - d_used_cap;
            print_c(&pad_right(" *Unallocated*", 15), VGA_DARK_GREY);
            print_c(&pad_right(&format!("| {}", d_name), 15), VGA_DARK_GREY);
            print_c(&pad_right("| None", 15), VGA_DARK_GREY);
            print_c(&pad_right(&format!("| {}", format_size_dec(unalloc)), 15), VGA_DARK_GREY);
            print_c("| Available for Use\n", VGA_DARK_GREY);
        } else {}
        unsafe { device_release(disk_ptr); }
    }
    print_c("========================================================================================================================\n", VGA_LIGHT_CYAN);
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