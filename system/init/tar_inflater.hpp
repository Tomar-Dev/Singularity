// system/init/tar_inflater.hpp
#ifndef TAR_INFLATER_HPP
#define TAR_INFLATER_HPP
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
void tar_inflater_init(void* addr, uint32_t size);
#ifdef __cplusplus
}
#endif

#endif