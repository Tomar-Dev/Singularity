// system/ffi/ffi.hpp
#ifndef FFI_BRIDGE_HPP
#define FFI_BRIDGE_HPP

#include "system/ffi/ffi.h"
#include "kernel/fastops.h"
#include "kernel/config.h"
#include <stdint.h>
#include <stddef.h>

struct FFIStats {
    uint64_t call_count;
    uint64_t total_cycles;
};

namespace MeowFFI {

    // FIX: Üretim Modunda (Release) FFI maliyetini fiziksel olarak sıfırlamak için
    // profil alma işlemleri log_level'a bağlandı.
    #if 0 // İstenirse kconfig.log_level > 2 gibi bir şarta bağlanabilir
        #define PROFILE_FFI(stat_obj, func_call) \
            uint64_t start_tsc = rdtsc_ordered(); \
            auto ret = func_call; \
            __atomic_fetch_add(&(stat_obj).call_count, 1, __ATOMIC_RELAXED); \
            __atomic_fetch_add(&(stat_obj).total_cycles, rdtsc_ordered() - start_tsc, __ATOMIC_RELAXED); \
            return ret;

        #define PROFILE_FFI_VOID(stat_obj, func_call) \
            uint64_t start_tsc = rdtsc_ordered(); \
            func_call; \
            __atomic_fetch_add(&(stat_obj).call_count, 1, __ATOMIC_RELAXED); \
            __atomic_fetch_add(&(stat_obj).total_cycles, rdtsc_ordered() - start_tsc, __ATOMIC_RELAXED);
    #else
        #define PROFILE_FFI(stat_obj, func_call) return func_call;
        #define PROFILE_FFI_VOID(stat_obj, func_call) func_call;
    #endif

    void print_ffi_stats();
}

#endif