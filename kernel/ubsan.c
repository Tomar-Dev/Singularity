// kernel/ubsan.c
#include "kernel/debug.h"
#include "libc/stdio.h"
#include <stdint.h>

#define UBSAN_HOOK __attribute__((used, noinline))

typedef struct {
    const char* filename;
    uint32_t    line;
    uint32_t    column;
} SourceLocation;

typedef struct {
    uint16_t    kind;
    uint16_t    info;
    const char* type_name;
} TypeDescriptor;

typedef struct {
    SourceLocation   location;
    const TypeDescriptor* type;
} OverflowData;

typedef struct {
    SourceLocation        location;
    const TypeDescriptor* from_type;
    const TypeDescriptor* to_type;
} ImplicitConversionData;

typedef struct {
    SourceLocation        location;
    const TypeDescriptor* type;
    uint8_t               log_alignment;
    uint8_t               type_check_kind;
} TypeMismatchData;

typedef struct {
    SourceLocation        location;
    const TypeDescriptor* array_type;
    const TypeDescriptor* index_type;
} OutOfBoundsData;

typedef struct {
    SourceLocation        location;
    const TypeDescriptor* lhs_type;
    const TypeDescriptor* rhs_type;
} ShiftOutOfBoundsData;

typedef struct {
    SourceLocation location;
    SourceLocation attr_location;
    int            arg_index;
} NonNullArgData;

typedef struct {
    SourceLocation location;
} UnreachableData;

typedef struct {
    SourceLocation        location;
    const TypeDescriptor* from_type;
    const TypeDescriptor* to_type;
    uint8_t               kind;
} InvalidValueData;

static void ubsan_report(const SourceLocation* loc, const char* kind,
                         const char* extra) {
    char buf[256];
    const char* file = (loc && loc->filename) ? loc->filename : "?";
    uint32_t    line = loc ? loc->line : 0;

    if (extra && extra[0]) {
        // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
        snprintf(buf, sizeof(buf), "UBSan: %s | %s", kind, extra);
    } else {
        // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
        snprintf(buf, sizeof(buf), "UBSan: %s", kind);
    }
    panic_at(file, (int)line, KERR_UBSAN, buf);
}

static const char* type_name(const TypeDescriptor* t) {
    if (!t || !t->type_name) { return "?"; }
    return t->type_name;
}

#ifdef __cplusplus
extern "C" {
#endif

void UBSAN_HOOK __ubsan_handle_type_mismatch_v1(void* data, uintptr_t ptr) {
    if (!data) { panic_at("UBSAN", 0, KERR_UBSAN, "UBSan: Type Mismatch (no data)"); }
    TypeMismatchData* d = (TypeMismatchData*)data;

    char extra[128];
    const char* reason;
    if (ptr == 0) {
        reason = "null pointer dereference";
    } else if (d->log_alignment && (ptr & ((1u << d->log_alignment) - 1))) {
        reason = "misaligned access";
    } else {
        reason = "insufficient size";
    }
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    snprintf(extra, sizeof(extra), "%s on type '%s' (ptr=0x%lx)",
             reason, type_name(d->type), (unsigned long)ptr);
    ubsan_report(&d->location, "Type Mismatch", extra);
}

void UBSAN_HOOK __ubsan_handle_type_mismatch(void* data, uintptr_t ptr) {
    __ubsan_handle_type_mismatch_v1(data, ptr);
}

void UBSAN_HOOK __ubsan_handle_add_overflow(void* data, void* lhs, void* rhs) {
    (void)lhs; (void)rhs;
    if (!data) { panic_at("UBSAN", 0, KERR_UBSAN, "UBSan: Add Overflow"); }
    OverflowData* d = (OverflowData*)data;
    char extra[64];
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    snprintf(extra, sizeof(extra), "type: %s", type_name(d->type));
    ubsan_report(&d->location, "Addition Overflow", extra);
}

void UBSAN_HOOK __ubsan_handle_sub_overflow(void* data, void* lhs, void* rhs) {
    (void)lhs; (void)rhs;
    if (!data) { panic_at("UBSAN", 0, KERR_UBSAN, "UBSan: Sub Overflow"); }
    OverflowData* d = (OverflowData*)data;
    char extra[64];
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    snprintf(extra, sizeof(extra), "type: %s", type_name(d->type));
    ubsan_report(&d->location, "Subtraction Overflow", extra);
}

void UBSAN_HOOK __ubsan_handle_mul_overflow(void* data, void* lhs, void* rhs) {
    (void)lhs; (void)rhs;
    if (!data) { panic_at("UBSAN", 0, KERR_UBSAN, "UBSan: Mul Overflow"); }
    OverflowData* d = (OverflowData*)data;
    char extra[64];
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    snprintf(extra, sizeof(extra), "type: %s", type_name(d->type));
    ubsan_report(&d->location, "Multiplication Overflow", extra);
}

void UBSAN_HOOK __ubsan_handle_divrem_overflow(void* data, void* lhs, void* rhs) {
    (void)lhs; (void)rhs;
    if (!data) { panic_at("UBSAN", 0, KERR_UBSAN, "UBSan: DivRem Overflow"); }
    OverflowData* d = (OverflowData*)data;
    char extra[64];
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    snprintf(extra, sizeof(extra), "type: %s", type_name(d->type));
    ubsan_report(&d->location, "Division/Remainder Overflow", extra);
}

void UBSAN_HOOK __ubsan_handle_negate_overflow(void* data, void* old_val) {
    (void)old_val;
    if (!data) { panic_at("UBSAN", 0, KERR_UBSAN, "UBSan: Negate Overflow"); }
    OverflowData* d = (OverflowData*)data;
    char extra[64];
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    snprintf(extra, sizeof(extra), "type: %s", type_name(d->type));
    ubsan_report(&d->location, "Negation Overflow", extra);
}

void UBSAN_HOOK __ubsan_handle_pointer_overflow(void* data, void* base, void* result) {
    if (!data) { panic_at("UBSAN", 0, KERR_UBSAN, "UBSan: Pointer Overflow"); }
    OverflowData* d = (OverflowData*)data;
    char extra[128];
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    snprintf(extra, sizeof(extra), "base=0x%lx result=0x%lx",
             (unsigned long)(uintptr_t)base, (unsigned long)(uintptr_t)result);
    ubsan_report(&d->location, "Pointer Overflow", extra);
}

void UBSAN_HOOK __ubsan_handle_shift_out_of_bounds(void* data, void* lhs, void* rhs) {
    (void)lhs; (void)rhs;
    if (!data) { panic_at("UBSAN", 0, KERR_UBSAN, "UBSan: Shift OOB"); }
    ShiftOutOfBoundsData* d = (ShiftOutOfBoundsData*)data;
    char extra[128];
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    snprintf(extra, sizeof(extra), "lhs_type='%s' rhs_type='%s'",
             type_name(d->lhs_type), type_name(d->rhs_type));
    ubsan_report(&d->location, "Shift Out of Bounds", extra);
}

void UBSAN_HOOK __ubsan_handle_out_of_bounds(void* data, void* index) {
    (void)index;
    if (!data) { panic_at("UBSAN", 0, KERR_UBSAN, "UBSan: Array OOB"); }
    OutOfBoundsData* d = (OutOfBoundsData*)data;
    char extra[128];
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    snprintf(extra, sizeof(extra), "array='%s' index_type='%s'",
             type_name(d->array_type), type_name(d->index_type));
    ubsan_report(&d->location, "Array Out of Bounds", extra);
}

void UBSAN_HOOK __ubsan_handle_out_of_bounds_v1(void* data, void* index) {
    __ubsan_handle_out_of_bounds(data, index);
}

void UBSAN_HOOK __ubsan_handle_load_invalid_value(void* data, void* val) {
    (void)val;
    if (!data) { panic_at("UBSAN", 0, KERR_UBSAN, "UBSan: Invalid Value"); }
    InvalidValueData* d = (InvalidValueData*)data;
    char extra[64];
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    snprintf(extra, sizeof(extra), "type: %s", type_name(d->from_type));
    ubsan_report(&d->location, "Load Invalid Value", extra);
}

void UBSAN_HOOK __ubsan_handle_builtin_unreachable(void* data) {
    if (!data) { panic_at("UBSAN", 0, KERR_UBSAN, "UBSan: Unreachable Code"); }
    UnreachableData* d = (UnreachableData*)data;
    ubsan_report(&d->location, "Reached Unreachable Code", "");
}

void UBSAN_HOOK __ubsan_handle_missing_return(void* data) {
    if (!data) { panic_at("UBSAN", 0, KERR_UBSAN, "UBSan: Missing Return"); }
    UnreachableData* d = (UnreachableData*)data;
    ubsan_report(&d->location, "Missing Return Statement", "");
}

void UBSAN_HOOK __ubsan_handle_vla_bound_not_positive(void* data, void* bound) {
    (void)bound;
    if (!data) { panic_at("UBSAN", 0, KERR_UBSAN, "UBSan: VLA Bound"); }
    OverflowData* d = (OverflowData*)data;
    ubsan_report(&d->location, "VLA Bound Not Positive", "");
}

void UBSAN_HOOK __ubsan_handle_nonnull_arg(void* data) {
    if (!data) { panic_at("UBSAN", 0, KERR_UBSAN, "UBSan: Nonnull Arg"); }
    NonNullArgData* d = (NonNullArgData*)data;
    char extra[64];
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    snprintf(extra, sizeof(extra), "arg_index=%d", d->arg_index);
    ubsan_report(&d->location, "Null Argument (nonnull)", extra);
}

void UBSAN_HOOK __ubsan_handle_nonnull_return_v1(void* data, void* loc) {
    (void)loc;
    if (!data) { panic_at("UBSAN", 0, KERR_UBSAN, "UBSan: Nonnull Return"); }
    NonNullArgData* d = (NonNullArgData*)data;
    ubsan_report(&d->location, "Non-Null Return Violation", "");
}

void UBSAN_HOOK __ubsan_handle_nullability_arg(void* data) {
    if (!data) { panic_at("UBSAN", 0, KERR_UBSAN, "UBSan: Nullability Arg"); }
    NonNullArgData* d = (NonNullArgData*)data;
    char extra[64];
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    snprintf(extra, sizeof(extra), "arg_index=%d", d->arg_index);
    ubsan_report(&d->location, "Nullability Argument Violation", extra);
}

void UBSAN_HOOK __ubsan_handle_nullability_return_v1(void* data, void* loc) {
    (void)loc;
    if (!data) { panic_at("UBSAN", 0, KERR_UBSAN, "UBSan: Nullability Return"); }
    NonNullArgData* d = (NonNullArgData*)data;
    ubsan_report(&d->location, "Nullability Return Violation", "");
}

void UBSAN_HOOK __ubsan_handle_float_cast_overflow(void* data, void* from) {
    (void)from;
    if (!data) { panic_at("UBSAN", 0, KERR_UBSAN, "UBSan: Float Cast Overflow"); }
    ImplicitConversionData* d = (ImplicitConversionData*)data;
    char extra[128];
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    snprintf(extra, sizeof(extra), "'%s' -> '%s'",
             type_name(d->from_type), type_name(d->to_type));
    ubsan_report(&d->location, "Float Cast Overflow", extra);
}

void UBSAN_HOOK __ubsan_handle_function_type_mismatch(void* data, void* func) {
    (void)func;
    if (!data) { panic_at("UBSAN", 0, KERR_UBSAN, "UBSan: Function Type Mismatch"); }
    TypeMismatchData* d = (TypeMismatchData*)data;
    ubsan_report(&d->location, "Function Type Mismatch", type_name(d->type));
}

void UBSAN_HOOK __ubsan_handle_invalid_builtin(void* data) {
    (void)data;
    panic_at("UBSAN", 0, KERR_UBSAN, "UBSan: Invalid Builtin");
}

void UBSAN_HOOK __ubsan_handle_implicit_conversion(void* data, void* from, void* to) {
    (void)from; (void)to;
    if (!data) { panic_at("UBSAN", 0, KERR_UBSAN, "UBSan: Implicit Conversion"); }
    ImplicitConversionData* d = (ImplicitConversionData*)data;
    char extra[128];
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    snprintf(extra, sizeof(extra), "'%s' -> '%s'",
             type_name(d->from_type), type_name(d->to_type));
    ubsan_report(&d->location, "Implicit Integer Conversion", extra);
}

#ifdef __cplusplus
}
#endif