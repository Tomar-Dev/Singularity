// rust_core/src/sys/topology.rs

use core::ffi::c_void;

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct CpuTopology {
    pub apic_id: u32,
    pub package_id: u32,
    pub core_id: u32,
    pub thread_id: u32,
    pub is_p_core: bool,
}

unsafe extern "C" {
    fn get_topology_array_ptr() -> *mut c_void;
}

pub fn get_topology(cpu_id: usize) -> CpuTopology {
    if cpu_id >= 32 {
        return CpuTopology {
            apic_id: cpu_id as u32,
            package_id: 0,
            core_id: cpu_id as u32,
            thread_id: 0,
            is_p_core: true,
        };
    }

    unsafe {
        let base_ptr = get_topology_array_ptr() as *const CpuTopology;
        *base_ptr.add(cpu_id)
    }
}

pub fn get_distance(cpu_a: usize, cpu_b: usize) -> u8 {
    if cpu_a == cpu_b { return 0; }

    let top_a = get_topology(cpu_a);
    let top_b = get_topology(cpu_b);

    if top_a.package_id != top_b.package_id {
        return 3; 
    }

    if top_a.core_id != top_b.core_id {
        return 2; 
    }

    1 
}