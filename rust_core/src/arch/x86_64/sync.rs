// rust_core/src/arch/x86_64/sync.rs

use core::sync::atomic::{AtomicBool, Ordering};
use core::cell::UnsafeCell;
use core::ops::{Deref, DerefMut};
use core::arch::asm;

pub struct IrqSpinlock<T> {
    locked: AtomicBool,
    data: UnsafeCell<T>,
}

unsafe impl<T: Send> Sync for IrqSpinlock<T> {}
unsafe impl<T: Send> Send for IrqSpinlock<T> {}

impl<T> IrqSpinlock<T> {
    pub const fn new(data: T) -> Self {
        Self {
            locked: AtomicBool::new(false),
            data: UnsafeCell::new(data),
        }
    }

    pub fn lock(&self) -> IrqSpinlockGuard<'_, T> {
        let rflags: u64;
        
        unsafe {
            asm!("pushfq; pop {}", out(reg) rflags, options(nomem, preserves_flags));
            asm!("cli", options(nomem, nostack));
        }

        while self.locked.compare_exchange(false, true, Ordering::Acquire, Ordering::Relaxed).is_err() {
            unsafe { asm!("pause", options(nomem, nostack)); }
        }

        IrqSpinlockGuard {
            lock: self,
            saved_rflags: rflags,
        }
    }

    pub fn try_lock(&self) -> Option<IrqSpinlockGuard<'_, T>> {
        let rflags: u64;
        
        unsafe {
            asm!("pushfq; pop {}", out(reg) rflags, options(nomem, preserves_flags));
            asm!("cli", options(nomem, nostack));
        }

        if self.locked.compare_exchange(false, true, Ordering::Acquire, Ordering::Relaxed).is_ok() {
            Some(IrqSpinlockGuard {
                lock: self,
                saved_rflags: rflags,
            })
        } else {
            if (rflags & 0x200) != 0 {
                unsafe { asm!("sti", options(nomem, nostack)); }
            }
            None
        }
    }
}

pub struct IrqSpinlockGuard<'a, T> {
    lock: &'a IrqSpinlock<T>,
    saved_rflags: u64,
}

impl<'a, T> Deref for IrqSpinlockGuard<'a, T> {
    type Target = T;
    fn deref(&self) -> &T {
        unsafe { &*self.lock.data.get() }
    }
}

impl<'a, T> DerefMut for IrqSpinlockGuard<'a, T> {
    fn deref_mut(&mut self) -> &mut T {
        unsafe { &mut *self.lock.data.get() }
    }
}

impl<'a, T> Drop for IrqSpinlockGuard<'a, T> {
    fn drop(&mut self) {
        self.lock.locked.store(false, Ordering::Release);

        if (self.saved_rflags & 0x200) != 0 {
            unsafe { asm!("sti", options(nomem, nostack)); }
        }
    }
}