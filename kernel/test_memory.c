// kernel/test_memory.c
#include "kernel/test_framework.h"
#include "memory/kheap.h"
#include "libc/stdio.h"
#include "libc/string.h"

TEST(memory_double_free) {
    printf("   [TEST] Double Free Detection... ");
    void* p = kmalloc(64);
    ASSERT_TEST(p != NULL);
    
    kfree(p);
    
    // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
    kfree(p); 
    
    printf("OK (Check serial logs for alert)\n");
}

TEST(memory_overflow) {
    printf("   [TEST] Integer Overflow Protection... ");
    
    size_t huge = (size_t)-1;
    void* p = kmalloc(huge);
    ASSERT_TEST(p == NULL);
    
    void* p2 = kcalloc(huge, 2);
    ASSERT_TEST(p2 == NULL);
    
    printf("OK\n");
}

TEST(memory_alignment) {
    printf("   [TEST] Aligned Allocation... ");
    
    void* p = kmalloc_aligned(128, 4096);
    ASSERT_TEST(p != NULL);
    ASSERT_TEST(((uintptr_t)p & 0xFFF) == 0);
    
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    memset(p, 0xCC, 128);
    
    kfree_aligned(p);
    printf("OK\n");
}

TEST(memory_recycled_magic) {
    printf("   [TEST] Recycled Block Magic... ");
    
    void* p1 = kmalloc(32);
    kfree(p1);
    
    void* p2 = kmalloc(32);
    
    ASSERT_TEST(p1 == p2);
    kfree(p2);
    
    printf("OK\n");
}

void run_memory_tests() {
    printf("\n--- Running Kernel Self-Tests (Phase 1A) ---\n");
    test_memory_double_free();
    test_memory_overflow();
    test_memory_alignment();
    test_memory_recycled_magic();
    printf("--- All Tests Passed ---\n\n");
}