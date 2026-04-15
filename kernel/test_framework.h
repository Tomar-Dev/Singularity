// kernel/test_framework.h
#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdint.h>
#include "kernel/debug.h"

// FIX: __attribute__((used)) eklenerek Clang'in unused warning vermesi engellendi.
#define TEST(name) __attribute__((used)) static void test_##name(void)

#define ASSERT_TEST(cond) \
    if (!(cond)) { \
        panic_at(__FILE__, __LINE__, KERR_ASSERT_FAIL, "TEST FAILED: " #cond); \
    }

void run_memory_tests();

#endif