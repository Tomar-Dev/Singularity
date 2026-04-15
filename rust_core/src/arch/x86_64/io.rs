// rust_core/src/arch/x86_64/io.rs

use core::arch::asm;
use core::marker::PhantomData;

#[inline]
pub unsafe fn outb(port: u16, value: u8) {
    unsafe { asm!("out dx, al", in("dx") port, in("al") value, options(nomem, nostack, preserves_flags)); }
}

#[inline]
pub unsafe fn inb(port: u16) -> u8 {
    let value: u8;
    unsafe { asm!("in al, dx", out("al") value, in("dx") port, options(nomem, nostack, preserves_flags)); }
    value
}

#[inline]
pub unsafe fn outw(port: u16, value: u16) {
    unsafe { asm!("out dx, ax", in("dx") port, in("ax") value, options(nomem, nostack, preserves_flags)); }
}

#[inline]
pub unsafe fn inw(port: u16) -> u16 {
    let value: u16;
    unsafe { asm!("in ax, dx", out("ax") value, in("dx") port, options(nomem, nostack, preserves_flags)); }
    value
}

#[inline]
pub unsafe fn outl(port: u16, value: u32) {
    unsafe { asm!("out dx, eax", in("dx") port, in("eax") value, options(nomem, nostack, preserves_flags)); }
}

#[inline]
pub unsafe fn inl(port: u16) -> u32 {
    let value: u32;
    unsafe { asm!("in eax, dx", out("eax") value, in("dx") port, options(nomem, nostack, preserves_flags)); }
    value
}

#[inline]
#[allow(dead_code)]
pub unsafe fn io_wait() {
    unsafe { outb(0x80, 0); }
}

pub trait InOut {
    unsafe fn port_in(port: u16) -> Self;
    unsafe fn port_out(port: u16, value: Self);
}

impl InOut for u8 {
    unsafe fn port_in(port: u16) -> Self { unsafe { inb(port) } }
    unsafe fn port_out(port: u16, value: Self) { unsafe { outb(port, value) } }
}

impl InOut for u16 {
    unsafe fn port_in(port: u16) -> Self { unsafe { inw(port) } }
    unsafe fn port_out(port: u16, value: Self) { unsafe { outw(port, value) } }
}

impl InOut for u32 {
    unsafe fn port_in(port: u16) -> Self { unsafe { inl(port) } }
    unsafe fn port_out(port: u16, value: Self) { unsafe { outl(port, value) } }
}

pub trait PortRead {}
pub trait PortWrite {}

#[derive(Debug, Clone, Copy)]
pub struct Port<T: InOut, P> {
    port: u16,
    phantom: PhantomData<(T, P)>,
}

pub struct ReadOnly;
impl PortRead for ReadOnly {}

pub struct WriteOnly;
impl PortWrite for WriteOnly {}

pub struct ReadWrite;
impl PortRead for ReadWrite {}
impl PortWrite for ReadWrite {}

impl<T: InOut, P> Port<T, P> {
    pub const fn new(port: u16) -> Self {
        Self {
            port,
            phantom: PhantomData,
        }
    }
}

impl<T: InOut, P: PortRead> Port<T, P> {
    #[inline]
    pub unsafe fn read(&self) -> T {
        unsafe { T::port_in(self.port) }
    }
}

impl<T: InOut, P: PortWrite> Port<T, P> {
    #[inline]
    pub unsafe fn write(&self, value: T) {
        unsafe { T::port_out(self.port, value) }
    }
}

pub type PortRO<T> = Port<T, ReadOnly>;
pub type PortWO<T> = Port<T, WriteOnly>;
pub type PortRW<T> = Port<T, ReadWrite>;