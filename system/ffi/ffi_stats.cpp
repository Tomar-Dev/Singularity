// system/ffi/ffi_stats.cpp
#include "system/ffi/ffi.hpp"
#include "libc/stdio.h"
#include "kernel/fastops.h"

FFIStats ffi_stat_pmm_alloc = {0, 0};
FFIStats ffi_stat_pmm_free = {0, 0};

namespace MeowFFI {

    void print_ffi_stats() {
        printf("\n========== RUST FFI BRIDGE METRICS ==========\n");
        printf("  (Zero-Overhead FFI Active. C++ Wrapper removed.)\n");
        printf("=============================================\n");
    }
}