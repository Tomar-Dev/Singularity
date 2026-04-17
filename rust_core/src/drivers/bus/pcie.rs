// rust_core/src/drivers/bus/pcie.rs

use crate::arch::io;
use crate::arch::sync::IrqSpinlock;
use crate::ffi::kprintf_string;
use core::ptr::{read_volatile, write_volatile};
use alloc::format;
use alloc::vec::Vec;
use alloc::string::{String, ToString};
use crate::kom::device::DeviceClass;

const CONFIG_ADDRESS: u16 = 0xCF8;
const CONFIG_DATA: u16 = 0xCFC;

#[derive(Clone, Copy)]
enum PcieBackend {
    Legacy,
    Pcie { ecam_virt: u64, start_bus: u8, end_bus: u8 },
}

static PCI_LEGACY_LOCK: IrqSpinlock<()> = IrqSpinlock::new(());
static PCIE_BACKEND: IrqSpinlock<PcieBackend> = IrqSpinlock::new(PcieBackend::Legacy);

#[derive(Clone, Copy)]
pub struct PcieDevice {
    pub bus: u8,
    pub dev: u8,
    pub func: u8,
    pub vendor_id: u16,
    pub device_id: u16,
    pub class_id: u8,
    pub subclass_id: u8,
    pub prog_if: u8,
    pub header_type: u8,
}

#[repr(C)]
pub struct FfiPcieDevice {
    pub bus: u8,
    pub dev: u8,
    pub func: u8,
    pub class_id: u8,
    pub subclass_id: u8,
    pub prog_if: u8,
    pub vendor_id: u16,
    pub device_id: u16,
    pub valid: bool,
}

static PCIE_DEVICES: IrqSpinlock<Vec<PcieDevice>> = IrqSpinlock::new(Vec::new());

impl PcieDevice {
    pub unsafe fn read_dword(bus: u8, dev: u8, func: u8, offset: u16) -> u32 {
        let backend = *PCIE_BACKEND.lock();
        match backend {
            PcieBackend::Pcie { ecam_virt, start_bus, end_bus } => {
                if bus >= start_bus && bus <= end_bus {
                    let phys_offset = (((bus - start_bus) as u64) << 20) 
                                    | ((dev as u64) << 15) 
                                    | ((func as u64) << 12) 
                                    | ((offset as u64) & 0xFFFC);
                    let ptr = (ecam_virt + phys_offset) as *const u32;
                    unsafe { read_volatile(ptr) }
                } else {
                    0xFFFFFFFF
                }
            },
            PcieBackend::Legacy => {
                if offset >= 256 { 
                    0xFFFFFFFF 
                } else {
                    let address = 0x80000000 | ((bus as u32) << 16) | ((dev as u32) << 11) | ((func as u32) << 8) | ((offset as u32) & 0xFFFC);
                    let _guard = PCI_LEGACY_LOCK.lock();
                    unsafe {
                        io::outl(CONFIG_ADDRESS, address);
                        io::inl(CONFIG_DATA)
                    }
                }
            }
        }
    }

    pub unsafe fn write_dword(bus: u8, dev: u8, func: u8, offset: u16, value: u32) {
        let backend = *PCIE_BACKEND.lock();
        match backend {
            PcieBackend::Pcie { ecam_virt, start_bus, end_bus } => {
                if bus >= start_bus && bus <= end_bus {
                    let phys_offset = (((bus - start_bus) as u64) << 20) 
                                    | ((dev as u64) << 15) 
                                    | ((func as u64) << 12) 
                                    | ((offset as u64) & 0xFFFC);
                    let ptr = (ecam_virt + phys_offset) as *mut u32;
                    unsafe { write_volatile(ptr, value); }
                } else {
                    // Out of bounds for ECAM segment
                }
            },
            PcieBackend::Legacy => {
                if offset >= 256 { 
                    return; 
                } else {
                    let address = 0x80000000 | ((bus as u32) << 16) | ((dev as u32) << 11) | ((func as u32) << 8) | ((offset as u32) & 0xFFFC);
                    let _guard = PCI_LEGACY_LOCK.lock();
                    unsafe {
                        io::outl(CONFIG_ADDRESS, address);
                        io::outl(CONFIG_DATA, value);
                    }
                }
            }
        }
    }

    pub unsafe fn read_word(bus: u8, dev: u8, func: u8, offset: u16) -> u16 {
        let backend = *PCIE_BACKEND.lock();
        match backend {
            PcieBackend::Pcie { ecam_virt, start_bus, end_bus } => {
                if bus >= start_bus && bus <= end_bus {
                    let phys_offset = (((bus - start_bus) as u64) << 20) | ((dev as u64) << 15) | ((func as u64) << 12) | (offset as u64);
                    let ptr = (ecam_virt + phys_offset) as *const u16;
                    unsafe { read_volatile(ptr) }
                } else { 
                    0xFFFF 
                }
            },
            PcieBackend::Legacy => {
                if offset >= 256 { 
                    0xFFFF 
                } else {
                    let address = 0x80000000 | ((bus as u32) << 16) | ((dev as u32) << 11) | ((func as u32) << 8) | ((offset as u32) & 0xFFFC);
                    let _guard = PCI_LEGACY_LOCK.lock();
                    unsafe {
                        io::outl(CONFIG_ADDRESS, address);
                        io::inw(CONFIG_DATA + (offset & 2))
                    }
                }
            }
        }
    }

    pub unsafe fn write_word(bus: u8, dev: u8, func: u8, offset: u16, value: u16) {
        let backend = *PCIE_BACKEND.lock();
        match backend {
            PcieBackend::Pcie { ecam_virt, start_bus, end_bus } => {
                if bus >= start_bus && bus <= end_bus {
                    let phys_offset = (((bus - start_bus) as u64) << 20) | ((dev as u64) << 15) | ((func as u64) << 12) | (offset as u64);
                    let ptr = (ecam_virt + phys_offset) as *mut u16;
                    unsafe { write_volatile(ptr, value); }
                } else {
                    // Ignore write
                }
            },
            PcieBackend::Legacy => {
                if offset < 256 {
                    let address = 0x80000000 | ((bus as u32) << 16) | ((dev as u32) << 11) | ((func as u32) << 8) | ((offset as u32) & 0xFFFC);
                    let _guard = PCI_LEGACY_LOCK.lock();
                    unsafe {
                        io::outl(CONFIG_ADDRESS, address);
                        io::outw(CONFIG_DATA + (offset & 2), value);
                    }
                } else {
                    // Out of bounds
                }
            }
        }
    }

    pub unsafe fn read_byte(bus: u8, dev: u8, func: u8, offset: u16) -> u8 {
        let backend = *PCIE_BACKEND.lock();
        match backend {
            PcieBackend::Pcie { ecam_virt, start_bus, end_bus } => {
                if bus >= start_bus && bus <= end_bus {
                    let phys_offset = (((bus - start_bus) as u64) << 20) | ((dev as u64) << 15) | ((func as u64) << 12) | (offset as u64);
                    let ptr = (ecam_virt + phys_offset) as *const u8;
                    unsafe { read_volatile(ptr) }
                } else { 
                    0xFF 
                }
            },
            PcieBackend::Legacy => {
                if offset >= 256 { 
                    0xFF 
                } else {
                    let address = 0x80000000 | ((bus as u32) << 16) | ((dev as u32) << 11) | ((func as u32) << 8) | ((offset as u32) & 0xFFFC);
                    let _guard = PCI_LEGACY_LOCK.lock();
                    unsafe {
                        io::outl(CONFIG_ADDRESS, address);
                        io::inb(CONFIG_DATA + (offset & 3))
                    }
                }
            }
        }
    }

    pub unsafe fn write_byte(bus: u8, dev: u8, func: u8, offset: u16, value: u8) {
        let backend = *PCIE_BACKEND.lock();
        match backend {
            PcieBackend::Pcie { ecam_virt, start_bus, end_bus } => {
                if bus >= start_bus && bus <= end_bus {
                    let phys_offset = (((bus - start_bus) as u64) << 20) | ((dev as u64) << 15) | ((func as u64) << 12) | (offset as u64);
                    let ptr = (ecam_virt + phys_offset) as *mut u8;
                    unsafe { write_volatile(ptr, value); }
                } else {
                    // Valid bounds broken
                }
            },
            PcieBackend::Legacy => {
                if offset < 256 {
                    let address = 0x80000000 | ((bus as u32) << 16) | ((dev as u32) << 11) | ((func as u32) << 8) | ((offset as u32) & 0xFFFC);
                    let _guard = PCI_LEGACY_LOCK.lock();
                    unsafe {
                        io::outl(CONFIG_ADDRESS, address);
                        io::outb(CONFIG_DATA + (offset & 3), value);
                    }
                } else {
                    // Exceeded layout structure
                }
            }
        }
    }

    pub fn wakeup(&self) {
        unsafe {
            let status = Self::read_word(self.bus, self.dev, self.func, 0x06);
            if (status & (1 << 4)) != 0 { 
                let mut cap_ptr = (Self::read_word(self.bus, self.dev, self.func, 0x34) & 0xFF) as u16;
                while cap_ptr != 0 {
                    let cap_reg = Self::read_word(self.bus, self.dev, self.func, cap_ptr);
                    let cap_id = (cap_reg & 0xFF) as u8;
                    let next_ptr = ((cap_reg >> 8) & 0xFF) as u16;
                    
                    if cap_id == 0x01 { 
                        let pmcsr_addr = cap_ptr + 4;
                        let mut pmcsr = Self::read_word(self.bus, self.dev, self.func, pmcsr_addr);
                        if (pmcsr & 0x03) != 0 { 
                            pmcsr &= !0x03; 
                            Self::write_word(self.bus, self.dev, self.func, pmcsr_addr, pmcsr);
                        } else {
                            // Already active D0
                        }
                        break;
                    } else {
                        // Not a Power Management capability
                    }
                    cap_ptr = next_ptr;
                }
            } else {
                // Capabilities list not supported
            }
        }
    }

    pub fn probe(bus: u8, dev: u8, func: u8) -> Option<Self> {
        let id_reg = unsafe { Self::read_dword(bus, dev, func, 0x00) };
        let vendor_id = (id_reg & 0xFFFF) as u16;
        if vendor_id == 0xFFFF { 
            return None; 
        } else {
            let device_id = (id_reg >> 16) as u16;
            let class_reg = unsafe { Self::read_dword(bus, dev, func, 0x08) };
            let subclass_id = ((class_reg >> 16) & 0xFF) as u8;
            let class_id = ((class_reg >> 24) & 0xFF) as u8;
            let prog_if = ((class_reg >> 8) & 0xFF) as u8;
            let header_type = ((unsafe { Self::read_dword(bus, dev, func, 0x0C) } >> 16) & 0xFF) as u8;

            Some(Self {
                bus, dev, func,
                vendor_id, device_id,
                class_id, subclass_id, prog_if,
                header_type
            })
        }
    }

    pub fn get_bar(&self, index: u8) -> u32 {
        if index > 5 { 
            return 0; 
        } else {
            unsafe { Self::read_dword(self.bus, self.dev, self.func, 0x10 + (index as u16 * 4)) }
        }
    }

    pub fn enable_bus_master(&self) {
        unsafe {
            let mut cmd = Self::read_word(self.bus, self.dev, self.func, 0x04);
            cmd |= 1 << 2;
            Self::write_word(self.bus, self.dev, self.func, 0x04, cmd);
        }
    }
    
    pub fn enable_memory_space(&self) {
        unsafe {
            let mut cmd = Self::read_word(self.bus, self.dev, self.func, 0x04);
            cmd |= 1 << 1;
            Self::write_word(self.bus, self.dev, self.func, 0x04, cmd);
        }
    }
}

pub fn find_device(class: u8, subclass: u8, prog_if: Option<u8>) -> Option<PcieDevice> {
    let devices = PCIE_DEVICES.lock();
    for p in devices.iter() {
        if p.class_id == class && p.subclass_id == subclass {
            if let Some(pi) = prog_if {
                if p.prog_if == pi { 
                    return Some(*p); 
                } else {
                    // Interface mismatch
                }
            } else {
                return Some(*p);
            }
        } else {
            // Class mismatch
        }
    }
    None
}

#[allow(unused)]
#[unsafe(no_mangle)]
pub extern "C" fn rust_pcie_init_ecam(mcfg_phys: u64, start_bus: u8, end_bus: u8) {
    let size = ((end_bus as u32) - (start_bus as u32) + 1) * 1048576;
    let ecam_virt = unsafe { crate::ffi::ioremap(mcfg_phys, size, 0x1B) } as u64; 
    if ecam_virt != 0 {
        *PCIE_BACKEND.lock() = PcieBackend::Pcie { ecam_virt, start_bus, end_bus };
    } else {
        unsafe { kprintf_string(c"[PCIe] Critical: Failed to map ECAM memory!\n".as_ptr()); }
    }
}

fn scan_bus(bus: u8, devices: &mut Vec<PcieDevice>, dev_count: &mut u32) {
    for dev in 0..32 {
        if let Some(p) = PcieDevice::probe(bus, dev, 0) {
            p.wakeup(); 
            devices.push(p);
            *dev_count += 1;
            
            let name = format!("pcie_{:02x}:{:02x}.0\0", bus, dev);
            crate::kom::device::register_device_silent(&name, DeviceClass::Bus, 0, 0, core::ptr::null_mut());
            
            if p.class_id == 0x06 && p.subclass_id == 0x04 {
                let bus_reg = unsafe { PcieDevice::read_dword(bus, dev, 0, 0x18) };
                let secondary_bus = ((bus_reg >> 8) & 0xFF) as u8;
                if secondary_bus > bus {
                    scan_bus(secondary_bus, devices, dev_count);
                } else {
                    // Circular bridge prevented
                }
            } else {
                // Not a bridge
            }

            if (p.header_type & 0x80) != 0 { 
                for f in 1..8 {
                    if let Some(pf) = PcieDevice::probe(bus, dev, f) {
                        pf.wakeup(); 
                        devices.push(pf);
                        *dev_count += 1;
                        
                        let fname = format!("pcie_{:02x}:{:02x}.{}\0", bus, dev, f);
                        crate::kom::device::register_device_silent(&fname, DeviceClass::Bus, 0, 0, core::ptr::null_mut());
                        
                        if pf.class_id == 0x06 && pf.subclass_id == 0x04 {
                            let bus_reg = unsafe { PcieDevice::read_dword(bus, dev, f, 0x18) };
                            let secondary_bus = ((bus_reg >> 8) & 0xFF) as u8;
                            if secondary_bus > bus {
                                scan_bus(secondary_bus, devices, dev_count);
                            } else {
                                // Circular bridge prevented
                            }
                        } else {
                            // Not a bridge
                        }
                    } else {
                        // Empty function
                    }
                }
            } else {
                // Not a multi-function device
            }
        } else {
            // Empty slot
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn rust_pcie_enumerate() {
    let mut devices = PCIE_DEVICES.lock();
    if !devices.is_empty() { 
        return; 
    } else {
        // Run Enumeration
    }
    
    let mut dev_count = 0;
    scan_bus(0, &mut devices, &mut dev_count);
    
    let msg = format!("Bus Enumerated. {} devices registered.\0", dev_count);
    unsafe {
        crate::ffi::exports::print_status(
            c"[ PCIe ]".as_ptr(),
            msg.as_ptr() as *const core::ffi::c_char,
            c"INFO".as_ptr()
        );
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn rust_pcie_get_device_count() -> u32 {
    PCIE_DEVICES.lock().len() as u32
}

#[unsafe(no_mangle)]
pub extern "C" fn rust_pcie_get_device_info(index: u32) -> FfiPcieDevice {
    let devices = PCIE_DEVICES.lock();
    if let Some(p) = devices.get(index as usize) {
        FfiPcieDevice {
            bus: p.bus, dev: p.dev, func: p.func,
            class_id: p.class_id, subclass_id: p.subclass_id, prog_if: p.prog_if,
            vendor_id: p.vendor_id, device_id: p.device_id,
            valid: true,
        }
    } else {
        FfiPcieDevice {
            bus: 0, dev: 0, func: 0, class_id: 0, subclass_id: 0, prog_if: 0,
            vendor_id: 0, device_id: 0, valid: false,
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn rust_pcie_read_dword(bus: u8, dev: u8, func: u8, offset: u16) -> u32 {
    unsafe { PcieDevice::read_dword(bus, dev, func, offset) }
}

#[unsafe(no_mangle)]
pub extern "C" fn rust_pcie_write_dword(bus: u8, dev: u8, func: u8, offset: u16, val: u32) {
    unsafe { PcieDevice::write_dword(bus, dev, func, offset, val) }
}

#[unsafe(no_mangle)]
pub extern "C" fn rust_pcie_read_word(bus: u8, dev: u8, func: u8, offset: u16) -> u16 {
    unsafe { PcieDevice::read_word(bus, dev, func, offset) }
}

#[unsafe(no_mangle)]
pub extern "C" fn rust_pcie_write_word(bus: u8, dev: u8, func: u8, offset: u16, val: u16) {
    unsafe { PcieDevice::write_word(bus, dev, func, offset, val) }
}

#[unsafe(no_mangle)]
pub extern "C" fn rust_pcie_read_byte(bus: u8, dev: u8, func: u8, offset: u16) -> u8 {
    unsafe { PcieDevice::read_byte(bus, dev, func, offset) }
}

#[unsafe(no_mangle)]
pub extern "C" fn rust_pcie_write_byte(bus: u8, dev: u8, func: u8, offset: u16, val: u8) {
    unsafe { PcieDevice::write_byte(bus, dev, func, offset, val) }
}

fn print_c(s: &str, fg: u8) {
    unsafe {
        // FIX: Replaced obsolete vga_set_color with unified console_set_color API
        crate::ffi::console_set_color(fg, 0); 
        let c_msg = format!("{}\0", s);
        kprintf_string(c_msg.as_ptr() as *const i8);
        crate::ffi::console_set_color(15, 0); 
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

#[unsafe(no_mangle)]
pub extern "C" fn pcie_print_all() {
    let devices = PCIE_DEVICES.lock();

    unsafe {
        crate::ffi::console_set_color(11, 0);
        kprintf_string(c"\n========== ".as_ptr());
        crate::ffi::console_set_color(15, 0);
        kprintf_string(c"PCIe BUS TOPOLOGY".as_ptr());
        crate::ffi::console_set_color(11, 0);
        kprintf_string(c" ==========\n".as_ptr());

        crate::ffi::console_set_color(15, 0);
        kprintf_string(c"\nADDR     | CLASS                  | VENDOR               | DEVICE\n".as_ptr());
        crate::ffi::console_set_color(8, 0);
        kprintf_string(c"---------+------------------------+----------------------+--------------\n".as_ptr());
    }

    let mut device_found = false;
    for dev_entry in devices.iter() {
        let device_type_str = match dev_entry.class_id {
            0x01 => match dev_entry.subclass_id {
                0x01 => "IDE Controller",
                0x06 => "SATA Controller",
                0x08 => "NVMe Controller",
                _ => "Mass Storage",
            },
            0x02 => "Network Controller",
            0x03 => "Display Controller",
            0x04 => "Multimedia Controller",
            0x05 => "Memory Controller",
            0x06 => match dev_entry.subclass_id {
                0x00 => "Host Bridge",
                0x01 => "ISA Bridge",
                0x04 => "PCI-to-PCI Bridge",
                _ => "Bridge Device",
            },
            0x0C => match dev_entry.subclass_id {
                0x03 => "USB Controller",
                0x05 => "SMBus",
                _ => "Serial Bus",
            },
            _ => "Unknown Class",
        };
        
        if device_type_str != "Unknown Class" {
            device_found = true;
            let addr_str = format!("{:02x}:{:02x}.{}", dev_entry.bus, dev_entry.dev, dev_entry.func);
            let class_padded = pad_right(device_type_str, 22);
            
            let vendor_str = if dev_entry.vendor_id == 0x8086 { "Intel Corporation" }
                             else if dev_entry.vendor_id == 0x1234 { "QEMU/Bochs" }
                             else if dev_entry.vendor_id == 0x1AF4 { "Red Hat (VirtIO)" }
                             else if dev_entry.vendor_id == 0x1B36 { "Red Hat (QEMU)" }
                             else if dev_entry.vendor_id == 0x15AD { "VMware" }
                             else if dev_entry.vendor_id == 0x80EE { "VirtualBox" }
                             else { "Generic Vendor" };
                             
            let vendor_padded = pad_right(vendor_str, 20);
            let dev_str = format!("{:04x}:{:04x}", dev_entry.vendor_id, dev_entry.device_id);

            print_c(&pad_right(&format!(" {}", addr_str), 9), 10);
            print_c("| ", 8);
            print_c(&format!("{} ", class_padded), 15);
            print_c("| ", 8);
            print_c(&format!("{} ", vendor_padded), 7);
            print_c("| ", 8);
            print_c(&format!("{}\n", dev_str), 15);
        } else {
            // Unregistered display omitted for clean topology output
        }
    }
    
    if !device_found {
        print_c(" No PCI/PCIe devices found by this driver.\n", 7);
    } else {
        // Displayed successfully
    }

    unsafe {
        crate::ffi::console_set_color(11, 0);
        kprintf_string(c"========================================================================\n".as_ptr());
    }
}