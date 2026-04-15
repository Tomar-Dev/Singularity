// rust_core/src/fs/utils.rs
use core::ptr;

#[inline(always)]
pub fn read_u8(buf: &[u8], offset: usize) -> u8 {
    if offset >= buf.len() { return 0; } else { return buf[offset]; }
}

#[inline(always)]
pub fn read_u16_le(buf: &[u8], offset: usize) -> u16 {
    if offset + 2 > buf.len() { return 0; } else { return u16::from_le_bytes([buf[offset], buf[offset + 1]]); }
}

#[inline(always)]
pub fn read_u32_le(buf: &[u8], offset: usize) -> u32 {
    if offset + 4 > buf.len() { return 0; } else { 
        let mut b = [0u8; 4];
        unsafe { ptr::copy_nonoverlapping(buf.as_ptr().add(offset), b.as_mut_ptr(), 4); }
        return u32::from_le_bytes(b);
    }
}

#[inline(always)]
pub fn read_u64_le(buf: &[u8], offset: usize) -> u64 {
    if offset + 8 > buf.len() { return 0; } else { 
        let mut b = [0u8; 8];
        unsafe { ptr::copy_nonoverlapping(buf.as_ptr().add(offset), b.as_mut_ptr(), 8); }
        return u64::from_le_bytes(b);
    }
}
