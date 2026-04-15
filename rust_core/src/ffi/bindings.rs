// rust_core/src/ffi/bindings.rs
// DİKKAT: Bu dosya C++ tarafındaki `storage_hal.h`, `cpu_hal.h` ve `kom_hal.h`
// başlıklarıyla %100 ABI uyumlu olmak ZORUNDADIR. Yapılarda değişiklik olursa
// iki taraf da aynı anda güncellenmelidir!

use core::ffi::c_void;

// --- storage_hal.h Karşılıkları ---

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct StorageKioVec {
    pub phys_addr: u64,
    pub virt_addr: *mut c_void,
    pub size: usize,
}

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct StorageDmaChain {
    pub prp1: u64,
    pub prp2: u64,
    pub prp_list_virt: *mut c_void,
    pub total_bytes: u32,
    pub is_valid: bool,
}

// C/C++ tarafından sağlanan dışa aktarılmış fonksiyonları (Opaque Pointers vb.) 
// çağırmamız gerekirse buraya eklenecektir. Şu an için LTO sayesinde FFI sınırı 
// minimumda tutulmaktadır.