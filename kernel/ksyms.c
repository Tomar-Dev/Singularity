// kernel/ksyms.c
#include "kernel/ksyms.h"
#include "libc/string.h"
extern const struct kernel_symbol kernel_symbols[];
extern const uint32_t kernel_symbol_count;

const char* ksyms_resolve_symbol(uint64_t addr, uint64_t* offset) {
    if (kernel_symbol_count == 0) {
        *offset = 0;
        return NULL;
    }

    int low = 0;
    int high = kernel_symbol_count - 1;
    const char* best_name = NULL;
    uint64_t best_addr = 0;

    while (low <= high) {
        int mid = low + (high - low) / 2;
        
        if (kernel_symbols[mid].addr == 0xFFFFFFFFFFFFFFFF) {
            high = mid - 1;
            continue;
        }

        if (kernel_symbols[mid].addr <= addr) {
            best_addr = kernel_symbols[mid].addr;
            best_name = kernel_symbols[mid].name;
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }

    if (best_name) {
        *offset = addr - best_addr;
        return best_name;
    }

    *offset = 0;
    return NULL;
}
