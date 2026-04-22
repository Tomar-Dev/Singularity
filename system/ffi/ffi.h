// system/ffi/ffi.h
#ifndef FFI_BRIDGE_H
#define FFI_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h> 

#ifdef __cplusplus
extern "C" {
#endif

void rust_device_print_disks(void);

uint32_t rust_device_detect_fs(void* dev);

void* rust_udf_mount(void* dev);

void rust_gpt_scan_partitions(void* dev_ptr);
int rust_gpt_create_partition(const char* dev_name, uint32_t size_mb, const char* name, const char* type, char* out_created_name);
int rust_gpt_delete_partition(const char* part_name);
int rust_gpt_init_disk(const char* dev_name);

void ffi_logger_task(void);
void ffi_logger_flush_sync(void);

#ifdef __cplusplus
}
#endif

#endif