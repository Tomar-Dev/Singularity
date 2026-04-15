// rust_core/src/sys/error.rs

#[repr(u32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[allow(dead_code)]
pub enum KernelError {
    Success      = 0x00,
    Unknown      = 0x01,
    
    Unsupported  = 0x05, // KOM_ERR_UNSUPPORTED
    
    RustPanic    = 0x50,
    RustOOM      = 0x51,
    RustSafeMem  = 0x52,
    
    OutOfBounds  = 0x27,
    NullDeref    = 0x26,
}