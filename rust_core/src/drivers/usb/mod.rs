// rust_core/src/drivers/usb/mod.rs

pub mod xhci;

#[unsafe(no_mangle)]
pub extern "C" fn rust_usb_init() {
    // FIX: Tamamen sessiz. Servis yöneticisi zaten "[ OK ] Started USB" yazacak.
    xhci::init(); 
}

#[unsafe(no_mangle)]
pub extern "C" fn rust_usb_print_ports() {
    xhci::print_ports();
}