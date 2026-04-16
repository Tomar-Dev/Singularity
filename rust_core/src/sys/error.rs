// rust_core/src/sys/error.rs

#[repr(u32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[allow(dead_code)]
pub enum KernelError {
    Success          = 0x00,
    Unknown          = 0x01,
    
    Unsupported      = 0x05, // KOM_ERR_UNSUPPORTED
    
    CorePanic        = 0x50, // Eski adı: RustPanic
    AllocatorOOM     = 0x51, // Eski adı: RustOOM
    SafeMemViolation = 0x52, // Eski adı: RustSafeMem
    
    OutOfBounds      = 0x27,
    NullDeref        = 0x26,
}