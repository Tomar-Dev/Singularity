// rust_core/src/drivers/usb/xhci.rs

use crate::drivers::bus::pcie;
use crate::ffi::{sync_print_color, ioremap};
// FIX: Doğru Rust fonksiyon ismi içe aktarıldı
use crate::memory::pmm::pmm_alloc_contiguous;
use core::ptr::{read_volatile, write_volatile};
use alloc::format;
use crate::arch::sync::IrqSpinlock;

#[repr(C, packed)]
#[derive(Clone, Copy)]
#[allow(dead_code)]
pub struct SetupPacket {
    pub bm_request_type: u8,
    pub b_request: u8,
    pub w_value: u16,
    pub w_index: u16,
    pub w_length: u16,
}

#[repr(C, packed)]
#[derive(Clone, Copy)]
#[allow(dead_code)]
pub struct DeviceDescriptor {
    pub b_length: u8,
    pub b_descriptor_type: u8,
    pub bcd_usb: u16,
    pub b_device_class: u8,
    pub b_device_sub_class: u8,
    pub b_device_protocol: u8,
    pub b_max_packet_size0: u8,
    pub id_vendor: u16,
    pub id_product: u16,
    pub bcd_device: u16,
    pub i_manufacturer: u8,
    pub i_product: u8,
    pub i_serial_number: u8,
    pub b_num_configurations: u8,
}

#[repr(C, packed)]
#[derive(Clone, Copy)]
#[allow(dead_code)]
pub struct Trb {
    pub parameter: u64,
    pub status: u32,
    pub control: u32,
}

#[allow(dead_code)]
struct XhciController {
    virt_base: u64,
    op_base: u64,
    rt_base: u64,
    max_ports: u8,
    
    cmd_ring_phys: u64,
    cmd_ring_virt: *mut Trb,
    dcbaap_phys: u64,
    dcbaap_virt: *mut u64,
}

unsafe impl Send for XhciController {}
unsafe impl Sync for XhciController {}

static XHCI: IrqSpinlock<Option<XhciController>> = IrqSpinlock::new(None);

const PAGE_FLAGS_MMIO: u64 = 0x01 | 0x02 | 0x10 | 0x08; 

const XHCI_USBCMD: u64 = 0x00;
const XHCI_USBSTS: u64 = 0x04;
const XHCI_CRCR:   u64 = 0x18;
const XHCI_DCBAAP: u64 = 0x30;
const XHCI_CONFIG: u64 = 0x38;

pub fn init() -> bool {
    if let Some(dev) = pcie::find_device(0x0C, 0x03, Some(0x30)) {
        
        dev.enable_bus_master();
        dev.enable_memory_space();

        let bar0 = dev.get_bar(0);
        let mut base_phys = (bar0 & 0xFFFFFFF0) as u64;
        
        if (bar0 & 0x04) != 0 {
            let bar1 = dev.get_bar(1);
            base_phys |= (bar1 as u64) << 32;
        }

        if base_phys == 0 { 
            return false; 
        }

        unsafe {
            let virt_base = ioremap(base_phys, 65536, PAGE_FLAGS_MMIO) as u64;
            if virt_base == 0 { 
                return false; 
            }

            let cap_reg = read_volatile(virt_base as *const u32);
            let cap_length = (cap_reg & 0xFF) as u8;
            let hci_version = ((cap_reg >> 16) & 0xFFFF) as u16;
            
            if hci_version == 0 || hci_version == 0xFFFF { 
                return false; 
            }

            let hcsparams1 = read_volatile((virt_base + 4) as *const u32);
            let max_ports = ((hcsparams1 >> 24) & 0xFF) as u8;
            let max_slots = (hcsparams1 & 0xFF) as u8;

            let op_base = virt_base + (cap_length as u64);
            let rt_off = read_volatile((virt_base + 0x18) as *const u32) & !0x1F;
            let rt_base = virt_base + (rt_off as u64);

            let mut usbcmd = read_volatile((op_base + XHCI_USBCMD) as *const u32);
            usbcmd &= !1; 
            write_volatile((op_base + XHCI_USBCMD) as *mut u32, usbcmd);
            
            for _ in 0..1000 {
                if (read_volatile((op_base + XHCI_USBSTS) as *const u32) & 1) != 0 { 
                    break; 
                }
            }

            write_volatile((op_base + XHCI_USBCMD) as *mut u32, usbcmd | 2); 
            for _ in 0..10000 {
                if (read_volatile((op_base + XHCI_USBCMD) as *const u32) & 2) == 0 { 
                    break; 
                }
            }
            
            let dcbaap_ptr = pmm_alloc_contiguous(1); 
            let dcbaap_phys = dcbaap_ptr as u64; 
            let dcbaap_virt = ioremap(dcbaap_phys, 4096, PAGE_FLAGS_MMIO) as *mut u64;
            core::ptr::write_bytes(dcbaap_virt as *mut u8, 0, 4096);

            write_volatile((op_base + XHCI_CONFIG) as *mut u32, max_slots as u32);
            write_volatile((op_base + XHCI_DCBAAP) as *mut u32, dcbaap_phys as u32);
            write_volatile((op_base + XHCI_DCBAAP + 4) as *mut u32, (dcbaap_phys >> 32) as u32);

            let cmd_ring_ptr = pmm_alloc_contiguous(1); 
            let cmd_phys = cmd_ring_ptr as u64; 
            let cmd_virt = ioremap(cmd_phys, 4096, PAGE_FLAGS_MMIO) as *mut Trb;
            core::ptr::write_bytes(cmd_virt as *mut u8, 0, 4096);
            
            (*cmd_virt.add(255)).parameter = cmd_phys;
            (*cmd_virt.add(255)).control = (6 << 10) | 2 | 1; 
            
            write_volatile((op_base + XHCI_CRCR) as *mut u32, (cmd_phys as u32) | 1);
            write_volatile((op_base + XHCI_CRCR + 4) as *mut u32, (cmd_phys >> 32) as u32);

            usbcmd = read_volatile((op_base + XHCI_USBCMD) as *const u32);
            write_volatile((op_base + XHCI_USBCMD) as *mut u32, usbcmd | 1); 

            *XHCI.lock() = Some(XhciController {
                virt_base,
                op_base,
                rt_base,
                max_ports,
                cmd_ring_phys: cmd_phys,
                cmd_ring_virt: cmd_virt,
                dcbaap_phys,
                dcbaap_virt,
            });
        }
        return true;
    } else {
        return false;
    }
}

pub fn print_ports() {
    let lock = XHCI.lock();
    if let Some(ref ctrl) = *lock {
        sync_print_color(11, 0, "\n========== ");
        sync_print_color(15, 0, "xHCI ROOT HUB TOPOLOGY");
        sync_print_color(11, 0, " ==========\n");
        
        let mut connected_count = 0;
        for i in 0..ctrl.max_ports {
            let portsc_addr = ctrl.op_base + 0x400 + ((i as u64) * 16);
            let portsc = unsafe { read_volatile(portsc_addr as *const u32) };
            
            if (portsc & 1) != 0 {
                let speed = (portsc >> 10) & 0xF;
                let speed_str = match speed {
                    1 => "Full-speed (12 Mbps)[USB 2.0]",
                    2 => "Low-speed (1.5 Mbps)[USB 1.1]",
                    3 => "High-speed (480 Mbps)[USB 2.0]",
                    4 => "SuperSpeed (5 Gbps)[USB 3.0]",
                    5 => "SuperSpeedPlus (10 Gbps)[USB 3.1]",
                    _ => "Unknown Speed",
                };
                
                sync_print_color(15, 0, &format!("[PORT {:02}] Device Attached -> ", i + 1));
                sync_print_color(10, 0, &format!("{}\n", speed_str));
                connected_count += 1;
            }
        }
        
        if connected_count == 0 {
            sync_print_color(7, 0, " No USB devices are currently attached.\n");
        }
        sync_print_color(11, 0, "============================================\n");
    } else {
        sync_print_color(12, 0, "Error: xHCI Controller is not active.\n");
    }
}