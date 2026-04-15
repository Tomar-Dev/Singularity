// system/disk/gpt.h
#ifndef GPT_H
#define GPT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void gpt_scan_partitions(void* dev_ptr);
int gpt_create_partition(const char* dev_name, uint32_t size_mb, const char* name, const char* type, char* out_created_name);
int gpt_delete_partition(const char* part_name);

#ifdef __cplusplus
}
#endif

#endif
