// rust_core/src/sys/acpi.rs

use core::slice;

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct AcpiSleepData {
    pub slp_typ_a: u16,
    pub slp_typ_b: u16,
    pub found: bool,
}

// Memory-Safe AML (ACPI Machine Language) Package Parser
fn parse_aml_package(data: &[u8]) -> Option<(u16, u16)> {
    let mut offset = 0;
    
    // Scan for 0x12 (PackageOp)
    while offset < data.len() && data[offset] != 0x12 {
        offset += 1;
    }
    
    if offset >= data.len() { 
        return None; 
    } else {
        // Opcode found
    }
    
    let mut ptr = offset + 1; 
    
    if ptr >= data.len() { 
        return None; 
    } else {
        // Valid bounds
    }
    
    let lead = data[ptr];
    let bytes = lead >> 6;
    
    if bytes == 0 {
        ptr += 1;
    } else {
        ptr += (bytes + 1) as usize;
    }
    
    if ptr >= data.len() { 
        return None; 
    } else {
        // Valid bounds
    }
    
    let num_elements = data[ptr];
    ptr += 1;
    
    let mut found_values = 0;
    let mut typ_a = 0;
    let mut typ_b = 0;
    
    for _ in 0..num_elements {
        if ptr >= data.len() || found_values >= 2 { 
            break; 
        } else {
            // Keep parsing
        }
        
        let prefix = data[ptr];
        let mut val = 0;
        let mut is_val = false;
        
        if prefix == 0x0A && ptr + 1 < data.len() { 
            // BytePrefix
            val = data[ptr + 1] as u16;
            ptr += 2;
            is_val = true;
        } else if prefix == 0x0B && ptr + 2 < data.len() { 
            // WordPrefix
            val = (data[ptr + 1] as u16) | ((data[ptr + 2] as u16) << 8);
            ptr += 3;
            is_val = true;
        } else if prefix == 0x00 { 
            // ZeroOp
            val = 0;
            ptr += 1;
            is_val = true;
        } else if prefix == 0x01 { 
            // OneOp
            val = 1;
            ptr += 1;
            is_val = true;
        } else {
            // Unknown opcode, skip 1 byte
            ptr += 1; 
        }
        
        if is_val {
            if found_values == 0 {
                typ_a = val << 10;
                found_values += 1;
            } else if found_values == 1 {
                typ_b = val << 10;
                found_values += 1;
            } else {
                // Ignore extra values
            }
        } else {
            // Not a value type
        }
    }
    
    if found_values >= 2 {
        Some((typ_a, typ_b))
    } else {
        None
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_acpi_parse_dsdt(dsdt_ptr: *const u8, dsdt_len: u32, s5_out: *mut AcpiSleepData, s3_out: *mut AcpiSleepData) {
    if dsdt_ptr.is_null() || s5_out.is_null() || s3_out.is_null() || dsdt_len < 4 {
        return;
    } else {
        // Valid pointers
    }

    // Convert raw firmware pointer to safe Rust slice (Bounds Checking Enabled)
    let dsdt_data = unsafe { slice::from_raw_parts(dsdt_ptr, dsdt_len as usize) };
    
    unsafe {
        *s5_out = AcpiSleepData::default();
        *s3_out = AcpiSleepData::default();
    }

    // Safely search for _S5_ and _S3_ packages
    for i in 0..(dsdt_data.len() - 4) {
        if &dsdt_data[i..i+4] == b"_S5_" {
            if let Some((a, b)) = parse_aml_package(&dsdt_data[i..]) {
                unsafe {
                    (*s5_out).slp_typ_a = a;
                    (*s5_out).slp_typ_b = b;
                    (*s5_out).found = true;
                }
            } else {
                // Invalid package
            }
        } else if &dsdt_data[i..i+4] == b"_S3_" {
            if let Some((a, b)) = parse_aml_package(&dsdt_data[i..]) {
                unsafe {
                    (*s3_out).slp_typ_a = a;
                    (*s3_out).slp_typ_b = b;
                    (*s3_out).found = true;
                }
            } else {
                // Invalid package
            }
        } else {
            // Keep scanning
        }
    }
}