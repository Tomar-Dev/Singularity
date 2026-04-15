// system/shell/utils/sysinfo.hpp
#ifndef SHELL_SYSINFO_HPP
#define SHELL_SYSINFO_HPP

#include <stdint.h>

void show_system_info_all();
void show_memory_map();
void show_task_list();
void show_task_manager();

extern "C" uint64_t safe_div64(uint64_t dividend, uint64_t divisor);
extern "C" uint64_t safe_mod64(uint64_t dividend, uint64_t divisor);

#endif
