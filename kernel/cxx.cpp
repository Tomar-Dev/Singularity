// kernel/cxx.cpp
#include <stddef.h>
#include <stdint.h>
#include "libc/stdio.h" 

extern "C" {
    void* kmalloc(size_t size);
    void kfree(void* ptr);
    void* kmalloc_aligned(size_t size, size_t alignment);
    void kfree_aligned(void* ptr);
    void panic_at(const char* file, int line, int code, const char* message);
    
    extern void (*__init_array_start[])(void);
    extern void (*__init_array_end[])(void);
    
    void init_global_constructors() {
        size_t count = __init_array_end - __init_array_start;
        for (size_t i = 0; i < count; i++) {
            if (__init_array_start[i]) {
                __init_array_start[i]();
            } else {
                // Ignore null constructor
            }
        }
    }
}

// FIX: C++23 Standart Kütüphanesiz (Freestanding) Ortam Uyumluluğu
namespace std {
    enum class align_val_t : size_t {};
}

void* operator new(size_t size) { return kmalloc(size); }
void* operator new[](size_t size) { return kmalloc(size); }

void operator delete(void* ptr) noexcept { kfree(ptr); }
void operator delete(void* ptr, size_t size) noexcept { (void)size; kfree(ptr); }
void operator delete[](void* ptr) noexcept { kfree(ptr); }
void operator delete[](void* ptr, size_t size) noexcept { (void)size; kfree(ptr); }

void* operator new(size_t size, std::align_val_t al) {
    return kmalloc_aligned(size, static_cast<size_t>(al));
}
void* operator new[](size_t size, std::align_val_t al) {
    return kmalloc_aligned(size, static_cast<size_t>(al));
}
void operator delete(void* ptr, std::align_val_t al) noexcept {
    (void)al; kfree_aligned(ptr);
}
void operator delete[](void* ptr, std::align_val_t al) noexcept {
    (void)al; kfree_aligned(ptr);
}

extern "C" void __cxa_pure_virtual() {
    panic_at("CXX_RUNTIME", 0, 0x01, "Pure Virtual Function Call Detected! (VTable Corruption)");
}

extern "C" int __cxa_atexit(void (*)(void *), void *, void *) {
    return 0; 
}